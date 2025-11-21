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
#include "async/async_semaphore.hpp"

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
    AsyncSemaphore& pipelineSem,
    redis::connection& redisCli,
    const std::shared_ptr<CFAsyncClient>& cfCli,
    asio::thread_pool& pool,
    const std::string chunkId
) {
    AsyncSemaphoreGuard semGuard(pipelineSem);

    // get children that need update
    std::vector<std::string> needsUpdate;
    {
        const std::string setKey = VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX + chunkId;
        
        static const std::string script = R"(
            local m = redis.call('SMEMBERS', KEYS[1])
            redis.call('DEL', KEYS[1])
            return m
        )";
        
        redis::request req;
        req.push("EVAL", script, "1", setKey);
        
        redis::response<std::vector<std::string>> res;
        co_await redisCli.async_exec(req, res, asio::use_awaitable);
        
        needsUpdate = std::get<0>(res).value();
        if (needsUpdate.empty()) {
            std::cout << chunkId << " no children to update" << std::endl;
            co_return;
        }
    }

    const auto splitId = Chunk::parseIdStr(chunkId);
    std::unique_ptr<ChunkData> chunk;

    // if chunk is not a low-res chunk, get update flags
    if (chunkId[0] != 'l' || (chunkId[0] == 'l' && splitId.first == 2)) {
        std::vector<std::vector<std::string>> flagSets;
        {
            std::vector<std::string> args;
            args.reserve(2 + needsUpdate.size());

            args.push_back(R"(
                local results = {}
                for i, key in ipairs(KEYS) do
                    results[i] = redis.call('SMEMBERS', key)
                end
                redis.call('DEL', unpack(KEYS))
                return results
            )");
            args.push_back(std::to_string(needsUpdate.size()));
            for (const auto& id : needsUpdate)
                args.push_back(VARS::REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + id);

            redis::request req;
            req.push_range("EVAL", args);

            redis::response<std::vector<boost::redis::resp3::node>> res;
            co_await redisCli.async_exec(req, res, asio::use_awaitable);
            
            // parse the generic_response to extract nested arrays
            const auto& nodes = std::get<0>(res).value();
            
            // ok to start at i = 2 since we are sure we are passing in atleast one key to request
            // since get needs update will return if no children to update
            std::vector<std::string> arr;
            for (size_t i = 2; i < nodes.size(); ++i) {
                auto& node = nodes[i];
                if (node.depth == 1 && node.data_type == redis::resp3::type::array) {
                    // Start of a new array
                    flagSets.push_back(std::move(arr));
                    arr.clear();
                } else if (node.depth == 2 && node.data_type == redis::resp3::type::blob_string) {
                    // String element in the current array
                    std::cout << "node: " << std::string(node.value) << std::endl;
                    arr.push_back(std::string(node.value));
                }
            }
            flagSets.push_back(std::move(arr));
        }

        // parse update flag strings
        std::vector<Plot::UpdateFlags> updateFlags(needsUpdate.size());
        for (size_t i = 0; i < flagSets.size(); ++i)
            for (const auto& flag : flagSets[i])
                if (flag == VARS::REDIS_FLAG_METADATA_ONLY)
                    updateFlags[i].metadataOnly = true;
                else if (flag == VARS::REDIS_FLAG_SET_DEFAULT_JSON)
                    updateFlags[i].setDefaultJson = true;
                else if (flag == VARS::REDIS_FLAG_SET_DEFAULT_BUILD)
                    updateFlags[i].setDefaultBuild = true;
                else if (flag == VARS::REDIS_FLAG_NO_IMAGE_UPDATE)
                    updateFlags[i].noImageUpdate = true;

        if (splitId.first == 2)
            chunk = std::make_unique<BaseChunk>(chunkId, std::move(needsUpdate), std::move(updateFlags));
        else
            chunk = std::make_unique<DChunk>(chunkId, std::move(needsUpdate), std::move(updateFlags));
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
                redis.call('EXPIRE', set_key, ARGV[5])
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
            VARS::REDIS_UPDATE_QUEUE_PREFIX,
            "1800" // 30 mins
        );
        co_await redisCli.async_exec(req, res, asio::use_awaitable); 
    }

    std::cout << "done" << std::endl;
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
        VARS::PIPELINE_LIMIT * 2
    );

    // init Redis connection pool
    redis::config cfg;
    cfg.addr.host = "redis-16216.c15.us-east-1-4.ec2.redns.redis-cloud.com";
    cfg.addr.port = "16216";
    cfg.username = "default";
    cfg.password = std::getenv("REDIS_PASSWORD");
    cfg.health_check_interval = std::chrono::seconds(10);
    RedisPool redisPool(exec, cfg, VARS::PIPELINE_LIMIT / 2);
    redis::logger lg(boost::redis::logger::level::emerg);
    redis::connection redisBlockingConn(exec, lg); // separate connection for BRPOP
    redisBlockingConn.async_run(cfg, asio::detached);  

    AsyncSemaphore inFlight(exec, VARS::PIPELINE_LIMIT);
    std::unordered_set<std::string> inPipeline;

    for (;;) {
        if (killf.load(std::memory_order_relaxed))
            co_return;

        co_await inFlight.async_acquire();

        try {
            std::string chunkId;
            {
                redis::request req;
                redis::response<std::optional<std::array<std::string, 2>>> resp;
                req.push("BRPOP", VARS::REDIS_UPDATE_QUEUE_PREFIX, "5");
                co_await redisBlockingConn.async_exec(req, resp, asio::use_awaitable);

                const auto& result = std::get<0>(resp).value();
                if (!result.has_value())
                    continue;

                chunkId = (*result)[1]; // [0] is queue name, [1] is the value
            }

            std::cout << chunkId << std::endl;
            asio::co_spawn(exec, processChunk(
                inFlight,
                redisPool.get(), 
                cfCli, 
                threadPool, 
                std::move(chunkId)
            ), asio::detached);
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