#include <unordered_set>
#include <string>
#include <tuple>
#include <iterator>
#include <memory>

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
    std::string chunkId, 
    redis::connection& redisCli, 
    asio::thread_pool& pool, 
    std::shared_ptr<int> inFlight
) {
    ++ *inFlight;

    std::vector<std::string> needsUpdateStrs;
    {
        redis::request req;
        redis::response<std::vector<std::string>> res;
        req.push("SMEMBERS", VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX + chunkId);
        co_await redisCli.async_exec(req, res, asio::use_awaitable);
        needsUpdateStrs = std::get<0>(res).value();
    }
        

    // convert ids to int
    std::vector<std::uint64_t> needsUpdate;
    needsUpdate.reserve(needsUpdateStrs.size());
    for(const auto& idStr : needsUpdateStrs)
        needsUpdate.push_back(std::stoll(idStr, nullptr, 16));

    const auto chunkIdParts = ChunkData::parseChunkIdStr(chunkId);
    bool isBaseChunk = chunkId[0] == 'l' && std::get<0>(chunkIdParts) == 2;
    ChunkData chunk;

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
            chunk = BaseChunk(chunkId, needsUpdate, updateflags);
        else
            chunk = DChunk(chunkId, needsUpdate, updateflags);

    } else
        chunk = LChunk(chunkId, needsUpdate);

    // pipeline
    chunk.prep();

    // process chunk on thread pool
    co_await asio::co_spawn(pool.get_executor(), [&chunk]() -> asio::awaitable<void> {
        chunk.process();
        co_return;
    }, asio::use_awaitable);

    const auto nextChunkId = chunk.update();
    if (nextChunkId) {
        const nextLayer = 
        
        {
            redis::request req;
            redis::response<std::vector<std::optional<std::string>>> res;
            req.push("ADD", VARS::
        }
    }

    -- *inFlight;
}



int main() {

    const auto NUM_CORES = std::thread::hardware_concurrency();
    const auto IO_POOL_SIZE = NUM_CORES;
    const auto CPU_POOL_SIZE = std::max(1u, NUM_CORES - 1);

    sw::redis::ConnectionOptions redisOpts;
    redisOpts.host = "my-redis.example.com";
    redisOpts.port = 6379;                                
    redisOpts.user = "default";                            
    redisOpts.password = "super-secret";                   
    redisOpts.db = 0;                                      
    redisOpts.socket_timeout = std::chrono::milliseconds(200);
    redisOpts.connect_timeout = std::chrono::milliseconds(500);
    redisOpts.keep_alive = true;    
    
    sw::redis::ConnectionPoolOptions redisPoolOpts;
    redisPoolOpts.size = IO_POOL_SIZE;
    redisPoolOpts.wait_timeout = std::chrono::milliseconds(50);

    sw::redis::Redis redisCli(redisOpts, redisPoolOpts);

    ThreadPool ioPool(IO_POOL_SIZE);
    ThreadPool cpuPool(CPU_POOL_SIZE);
    std::counting_semaphore<> inflight(4);

    /* update loop */

    std::string chunkId;

    ioPool.post([&]{
        inflight.acquire();

        // get list of chunk members that need update
        std::unordered_set<std::string> needsUpdateStrs;
        redisCli.smembers(VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX + chunkId, std::inserter(needsUpdateStrs, needsUpdateStrs.begin()));

        // convert ids to int
        std::vector<std::uint64_t> needsUpdate;
        needsUpdate.reserve(needsUpdateStrs.size());
        for(const auto& idStr : needsUpdateStrs)
            needsUpdate.push_back(std::stoll(idStr, nullptr, 16));

        ChunkData chunk;

        const auto chunkIdParts = ChunkData::parseChunkIdStr(chunkId);
        bool isBaseChunk = chunkId[0] == 'l' && std::get<0>(chunkIdParts) == 2;

        if (chunkId[0] != 'l' || isBaseChunk) {
            // get update flags
            std::vector<std::string> getFlagsKeys;
            for (const auto &plotId : needsUpdateStrs)
                getFlagsKeys.push_back(VARS::REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + plotId);

            std::vector<std::optional<std::string>> flagStrs;
            redisCli.mget(getFlagsKeys.begin(), getFlagsKeys.end(), std::back_inserter(flagStrs));

            // parse flag strings
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
                chunk = BaseChunk(chunkId, needsUpdate, updateflags);
            else
                chunk = DChunk(chunkId, needsUpdate, updateflags);

        } else
            chunk = LChunk(chunkId, needsUpdate);

        // pipeline
        chunk.prep();
        cpuPool.post([&chunkId, &chunk, &ioPool, &inflight, &redisCli]{ 
            chunk.process(); 
            ioPool.post([&chunkId, &chunk, &inflight, &redisCli]{
                const auto nextChunkId = chunk.update();
                if (nextChunkId) {
                    // update to redis
                    redisCli.sadd(VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX, chunkId);
                    // todo queue
                }
                inflight.release();
            });
        });

    });

    return 0;
}