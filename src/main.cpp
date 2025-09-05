#include <unordered_set>
#include <string>
#include <tuple>
#include <iterator>
#include <memory>
#include <variant>

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/redis.hpp>
#include <boost/redis/connection.hpp>
#include <boost/redis/request.hpp>
#include <boost/redis/adapter/adapt.hpp>

#include "thread_pool/thread_pool.hpp"
#include "chunk/chunk_data.hpp"
#include "config/config.hpp"
#include "chunk/chunk_data.hpp"
#include "chunk/base_chunk.hpp"
#include "chunk/d_chunk.hpp"
#include "chunk/l_chunk.hpp"

namespace redis = boost::redis;
namespace asio = boost::asio;

asio::awaitable<void> processChunk(
    int qlvl,
    redis::connection& redisCli, 
    asio::thread_pool& pool, 
    std::shared_ptr<int> inFlight
) {
    ++ *inFlight;

    // pop chunk from queue, get children that need update
    std::string chunkId;
    std::vector<std::string> needsUpdateStrs;
    std::vector<std::uint64_t> needsUpdate;
    {
        static const std::string script = R"(
            local chunkId = redis.call('RPOP', KEYS[1])
            if not chunkId then
                return nil
            end
            
            local set_key = ARGV[1] .. chunkId
            local needsUpdate = redis.call('SMEMBERS', set_key)
            redis.call('DEL', set_key)
            
            return {chunkId, needsUpdate}
        )";
        redis::request req;
        redis::response<std::optional<std::tuple<std::string, std::vector<std::string>>>> res;
        req.push("EVAL", script, "1", std::format("{}{}:", VARS::REDIS_UPDATE_QUEUE_PREFIX, qlvl), VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX);
        co_await redisCli.async_exec(req, res, asio::use_awaitable);
        const auto result = std::get<0>(res).value();
        if (!result)
            co_return;

        chunkId = std::get<0>(*result);
        needsUpdateStrs = std::get<1>(*result);

        needsUpdate.reserve(needsUpdateStrs.size());
        for(const auto& idStr : needsUpdateStrs)
            needsUpdate.push_back(std::stoll(idStr, nullptr, 16));
    }

    const auto chunkIdParts = ChunkData::parseChunkIdStr(chunkId);
    bool isBaseChunk = chunkId[0] == 'l' && std::get<0>(chunkIdParts) == 2;
    std::unique_ptr<ChunkData> chunk;

    if (chunkId[0] != 'l' || isBaseChunk) {
        // make update flag keys
        std::vector<std::string> getFlagsKeys;
        for (const auto &plotId : needsUpdateStrs)
            getFlagsKeys.push_back(VARS::REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + plotId);

        // get update flags
        std::vector<std::optional<std::string>> flagStrs;
        {
            redis::request req;
            redis::response<std::vector<std::optional<std::string>>> res;
            req.push_range("MGET", getFlagsKeys.begin(), getFlagsKeys.end());
            co_await redisCli.async_exec(req, res, asio::use_awaitable);
            flagStrs = std::get<0>(res).value();
        }

        // parse update flag strings
        std::vector<UpdateFlags> updateflags(needsUpdateStrs.size());
        for (size_t i = 0; i < updateflags.size(); ++i) {
            if (flagStrs[i]) {
                std::stringstream ss(*flagStrs[i]);
                std::string token;
                
                while (std::getline(ss, token, ' ')) {
                    if (token == VARS::REDIS_FLAG_UPDATE_METADATA_FIELDS_ONLY)
                        updateflags[i].updateMetadataFieldsOnly = true;
                    else if (token == VARS::REDIS_FLAG_SET_DEFAULT_PLOT)
                        updateflags[i].setDefaultPlot = true;
                    else if (token == VARS::REDIS_FLAG_SET_DEFAULT_BUILD)
                        updateflags[i].setDefaultBuild = true;
                    else if (token == VARS::REDIS_FLAG_NO_IMAGE_UPDATE)
                        updateflags[i].noImageUpdate = true;
                }
            }
        }

        if (isBaseChunk)
            chunk = std::make_unique<BaseChunk>(chunkId, needsUpdate, updateflags);
        else
            chunk = std::make_unique<DChunk>(chunkId, needsUpdate, updateflags);

    } else
        chunk = std::make_unique<LChunk>(chunkId, needsUpdate);

    // pipeline
    co_await chunk->prep();

    // process chunk on thread pool
    co_await asio::co_spawn(pool.get_executor(), [&chunk]() -> asio::awaitable<void> {
        chunk->process();
        co_return;
    }, asio::use_awaitable);

    const auto nextChunkId = co_await chunk->update();
    if (nextChunkId) {
        int nextLayer = std::get<0>(ChunkData::parseChunkIdStr(*nextChunkId));
        static const std::string script = R"(
            local chunkId = ARGV[1]

            local set_key = ARGV[3] .. chunkId
            local existed = redis.call('EXISTS', set_key)
            local added = redis.call('SADD', set_key, ARGV[2])

            if existed == 0 and added > 0 then
                local queue_key = ARGV[4]
                redis.call('LPUSH', queue_key, chunkId)
            end
        )";
    
        redis::request req;
        redis::response<int> res;
        req.push(
            "EVAL", 
            script, 
            "0", 
            nextChunkId,
            chunkId,
            VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX,
            std::format("{}{}:", VARS::REDIS_UPDATE_QUEUE_PREFIX, nextLayer)
        );
        co_await redisCli.async_exec(req, res, asio::use_awaitable); 
    }

    -- *inFlight;
}



int main() {

    // const auto NUM_CORES = std::thread::hardware_concurrency();
    // const auto IO_POOL_SIZE = NUM_CORES;
    // const auto CPU_POOL_SIZE = std::max(1u, NUM_CORES - 1);

    // sw::redis::ConnectionOptions redisOpts;
    // redisOpts.host = "my-redis.example.com";
    // redisOpts.port = 6379;                                
    // redisOpts.user = "default";                            
    // redisOpts.password = "super-secret";                   
    // redisOpts.db = 0;                                      
    // redisOpts.socket_timeout = std::chrono::milliseconds(200);
    // redisOpts.connect_timeout = std::chrono::milliseconds(500);
    // redisOpts.keep_alive = true;    
    
    // sw::redis::ConnectionPoolOptions redisPoolOpts;
    // redisPoolOpts.size = IO_POOL_SIZE;
    // redisPoolOpts.wait_timeout = std::chrono::milliseconds(50);

    // sw::redis::Redis redisCli(redisOpts, redisPoolOpts);

   
    // ThreadPool cpuPool(CPU_POOL_SIZE);
    

    return 0;
}