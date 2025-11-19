#include <unordered_set>
#include <string>
#include <tuple>
#include <iterator>
#include <memory>
#include <variant>
#include <chrono>
#include <atomic>

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/redis/src.hpp>
#include <boost/redis/connection.hpp>
#include <boost/redis/request.hpp>
#include <boost/redis/adapter/adapt.hpp>
#include <boost/asio/ip/tcp.hpp>  
#include <fmt/format.h>

#include "chunk/chunk_data.hpp"
#include "config/config.hpp"
#include "chunk/chunk_data.hpp"
#include "chunk/chunk.hpp"
#include "chunk/types/base_chunk.hpp"
#include "chunk/types/d_chunk.hpp"
#include "chunk/types/l_chunk.hpp"
#include "async/cf_async_client.hpp"
#include "utils/plot.hpp"
#include "utils/utils.hpp"
#include "utils/redis_pool.hpp"

namespace redis = boost::redis;
namespace asio = boost::asio;

using namespace std::chrono_literals;  

class InFlightGuard {
private:
    std::shared_ptr<int> _if;
public:
    InFlightGuard(std::shared_ptr<int> _if) : _if(_if) { ++(*_if); }
    ~InFlightGuard() { --(*_if); }
};

static std::atomic<bool> killf = false;

asio::awaitable<void> processChunk(
    redis::connection& redisCli,
    const std::shared_ptr<CFAsyncClient>& cfCli,
    asio::thread_pool& pool,
    const std::string chunkId
) {
    try {
        // pop chunk from queue, get children that need update
        std::vector<std::string> needsUpdate;
        {
            static const std::string script = R"(
                local set_key = ARGV[1] .. KEYS[1]
                local needsUpdate = redis.call('SMEMBERS', set_key)
                redis.call('DEL', set_key)
                return needsUpdate
            )";
            
            redis::request req;
            redis::response<std::vector<std::string>> res;
            req.push("EVAL", script, "1", chunkId, VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX);
            co_await redisCli.async_exec(req, res, asio::use_awaitable);
            const auto& result = std::get<0>(res).value();

            if (result.empty())
                throw std::runtime_error(chunkId + " no children to update.");

            needsUpdate.reserve(result.size());
            for (size_t i = 0; i < result.size(); ++i) 
                needsUpdate.push_back(std::move(result[i]));
        }

        const auto splitId = Chunk::parseIdStr(chunkId);
        std::unique_ptr<ChunkData> chunk;

        if (chunkId[0] != 'l' || (chunkId[0] == 'l' && splitId.first == 2)) {

            // make update flag keys
            std::vector<std::string> getFlagsKeys;
            for (const auto &id : needsUpdate)
                getFlagsKeys.push_back(VARS::REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + id);

            // get update flags
            std::vector<std::optional<std::string>> flagStrs;
            {
                static const std::string script = R"(
                    local n = #KEYS
                    if n == 0 then return {} end
                    local vals = redis.call('MGET', unpack(KEYS))
                    redis.call('DEL', unpack(KEYS))
                    return vals
                )";
                redis::request req;
                redis::response<std::vector<std::optional<std::string>>> res;
                std::vector<std::string> args;
                args.reserve(2 + getFlagsKeys.size());
                args.emplace_back(script);
                args.emplace_back(std::to_string(getFlagsKeys.size()));
                args.insert(args.end(), getFlagsKeys.begin(), getFlagsKeys.end());

                req.push_range("EVAL", args);
                co_await redisCli.async_exec(req, res, asio::use_awaitable);
            
                flagStrs = std::get<0>(res).value();
            }

            // parse update flag strings
            std::vector<Plot::UpdateFlags> updateflags(needsUpdate.size());
            for (size_t i = 0; i < updateflags.size(); ++i) {
                if (flagStrs[i]) {
                    std::stringstream ss(*flagStrs[i]);
                    std::string token;
                    
                    while (std::getline(ss, token, ' ')) {
                        if (token == VARS::REDIS_FLAG_METADATA_ONLY)
                            updateflags[i].metadataOnly = true;
                        else if (token == VARS::REDIS_FLAG_SET_DEFAULT_JSON)
                            updateflags[i].setDefaultJson = true;
                        else if (token == VARS::REDIS_FLAG_SET_DEFAULT_BUILD)
                            updateflags[i].setDefaultBuild = true;
                        else if (token == VARS::REDIS_FLAG_NO_IMAGE_UPDATE)
                            updateflags[i].noImageUpdate = true;
                    }
                }
            }

            if (splitId.first == 2) {
                chunk = std::make_unique<BaseChunk>(chunkId, std::move(needsUpdate), std::move(updateflags));
            } else
                chunk = std::make_unique<DChunk>(chunkId, std::move(needsUpdate), std::move(updateflags));

        } else
            chunk = std::make_unique<LChunk>(chunkId, std::move(needsUpdate));

        // pipeline
        co_await chunk->prep(cfCli);
        // process chunk on thread pool
        co_await asio::co_spawn(pool.get_executor(), [&chunk]() mutable -> asio::awaitable<void> {
            chunk->process();
            co_return;
        }, asio::use_awaitable);

        const auto nextChunkId = co_await chunk->update(cfCli);
        if (nextChunkId) {
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
                *nextChunkId,
                fmt::format("{:x}", splitId.second),
                VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX,
                VARS::REDIS_UPDATE_QUEUE_PREFIX
            );
            co_await redisCli.async_exec(req, res, asio::use_awaitable); 
        }

        std::cout << "done" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[ex] " << e.what() << "\n";
    }
}


asio::awaitable<void> async_main() {
    const auto exec = co_await asio::this_coro::executor;

    // create thread pool with cores-1 threads
    asio::thread_pool threadPool(std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1));

    // init Cloudflare helper
    const std::shared_ptr<CFAsyncClient> cfCli = std::make_shared<CFAsyncClient>(
        "https://1534f5e1cce37d41a018df4c9716751e.r2.cloudflarestorage.com",
        std::getenv("CF_R2_ACCESS_KEY"),
        std::getenv("CF_R2_SECRET_KEY"),
        10
    );

    // init Redis connection pool
    redis::config cfg;
    cfg.addr.host = "redis-16216.c15.us-east-1-4.ec2.redns.redis-cloud.com";
    cfg.addr.port = "16216";
    cfg.username = "default";
    cfg.password = std::getenv("REDIS_PASSWORD");
    RedisPool redisPool(exec, cfg, 5);
    redis::logger lg(boost::redis::logger::level::emerg);
    redis::connection redisBlockingConn(exec, lg); // separate connection for BRPOP
    redisBlockingConn.async_run(cfg, asio::detached);  

    for (;;) {
        if (killf.load(std::memory_order_relaxed))
            co_return;

        //TODO add inflight limit
        try {
            std::string chunkId;
            {
                redis::request req;
                redis::response<std::optional<std::array<std::string, 2>>> resp;
                req.push("BRPOP", VARS::REDIS_UPDATE_QUEUE_PREFIX, "5");
                co_await redisBlockingConn.async_exec(req, resp, asio::use_awaitable);

                const auto& result = std::get<0>(resp).value();
                if (!result.has_value()) {
                    std::cout << "BRPOP returned null." << std::endl;
                    continue;
                }

                chunkId = (*result)[1]; // [0] is queue name, [1] is the value
            }
            std::cout << chunkId << std::endl;
            asio::co_spawn(exec, processChunk(redisPool.get(), cfCli, threadPool, std::move(chunkId)), asio::detached);
        } catch (const std::exception& e) {
            std::cerr << "[ex] " << e.what() << "\n";
        }
    }
    threadPool.join();
}

extern "C" const char* __asan_default_options() {
    return "detect_container_overflow=0";
}

int main() {
    Aws::SDKOptions s3Opts;
    Aws::InitAPI(s3Opts);

    char* envc = std::getenv("ENV");
    std::string env = envc ? envc : "";
    if (env != "PROD")
        Utils::loadENV(".env");

    // coroutine scheduler
    asio::io_context ioc;

    // handle sigint sigterm
    asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&ioc](const boost::system::error_code& ec, int) {
        if (!ec) {
            killf.store(true, std::memory_order_relaxed);
            ioc.stop();
        }
    });

    std::cout << "Starting" << std::endl;
    
    // create coroutine for main loop
    asio::co_spawn(ioc, async_main(), asio::detached);
    ioc.run();

    Aws::ShutdownAPI(s3Opts);
    return 0;
}