#include <unordered_set>
#include <string>
#include <tuple>
#include <iterator>
#include <memory>
#include <variant>
#include <chrono>
#include <atomic>
#include <queue>

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
#include "utils/delayed_updates.hpp"
#include "utils/unique_queue.hpp"

namespace redis = boost::redis;
namespace asio = boost::asio;

using namespace std::chrono_literals;  

class PipelineGuard {
private:
    const std::string& _chunkId;
    AsyncSemaphore& _sem;
    std::unordered_set<std::string>& _inPipeline;
public:
    PipelineGuard(
        AsyncSemaphore& sem, 
        std::unordered_set<std::string>& inPipeline, 
        const std::string& chunkId
    ) : _chunkId(chunkId), _sem(sem), _inPipeline(inPipeline) {};
    ~PipelineGuard() { 
        _inPipeline.erase(_chunkId);
        _sem.release();
    };
};

static std::atomic<bool> killFlag(false);
static std::shared_ptr<CFAsyncClient> cfCli;
static UniqueQueue needsPurge;

asio::awaitable<void> processChunk(
    RedisPool& redisPool,
    asio::thread_pool& cpuPool,
    DelayedUpdates& delayedUpdates,
    AsyncSemaphore& pipelineSem,
    std::unordered_set<std::string>& inPipeline,
    const std::string chunkId
) {
    PipelineGuard pg(pipelineSem, inPipeline, chunkId);

    try {
        // get children that need update
        std::vector<std::string> needsUpdate;
        {
            static const std::string script = R"(
                local m = redis.call('SMEMBERS', KEYS[1])
                redis.call('DEL', KEYS[1])
                return m
            )";

            const std::string setKey = VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX + chunkId;
            redis::request req;
            req.push("EVAL", script, "1", setKey);
            
            redis::response<std::vector<std::string>> res;
            co_await redisPool.get().async_exec(req, res, asio::use_awaitable);
            
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
                static const std::string script = R"(
                    local results = {}
                    for i, key in ipairs(KEYS) do
                        results[i] = redis.call('SMEMBERS', key)
                    end
                    redis.call('DEL', unpack(KEYS))
                    return results
                )";

                std::vector<std::string> args;
                args.reserve(2 + needsUpdate.size());
                args.push_back(script);
                args.push_back(std::to_string(needsUpdate.size()));
                for (const auto& id : needsUpdate)
                    args.push_back(VARS::REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + id);

                redis::request req;
                req.push_range("EVAL", args);

                redis::response<std::vector<boost::redis::resp3::node>> res;
                co_await redisPool.get().async_exec(req, res, asio::use_awaitable);
                
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
        co_await asio::co_spawn(cpuPool.get_executor(), [&chunk]() mutable -> asio::awaitable<void> {
            chunk->process();
            co_return;
        }, asio::use_awaitable);
        
        const auto nextChunkId = co_await chunk->update(cfCli);
        if (nextChunkId) {
            // schedule next layer to be updated
            const int64_t updateDelay = splitId.first-1 == 1 ? CONFIG::L1_UPDATE_DELAY_SEC : CONFIG::L0_UPDATE_DELAY_SEC;
            delayedUpdates.track(*nextChunkId, fmt::format("{:x}", splitId.second), updateDelay);
        }
        std::cout << chunkId << std::endl;

        // schedule chunk to be purged from cloudflare cache
        needsPurge.push(chunkId);
    } catch (const std::exception& e) {
        std::cerr << "[ex] " << e.what() << "\n";
    }
    co_return;
}

asio::awaitable<void> purgeChunks() {
    if (needsPurge.size() == 0)
        co_return;

    const size_t n = std::min(needsPurge.size(), VARS::PURGE_URLS_LIMIT);
    std::vector<std::string> urls;
    urls.reserve(n);
    
    for (size_t i = 0; i < n; ++i) {
        std::cout << "purging " << needsPurge.front() << std::endl;
        urls.push_back(VARS::CF_CHUNKS_BUCKET_URL + needsPurge.front());
        needsPurge.pop();
    }

    co_await cfCli->purgeCache(std::move(urls));
    co_return;
}

asio::awaitable<void> purgeLoop() {
    const auto exec = co_await asio::this_coro::executor;
    asio::steady_timer timer(exec);

    for (;;) {
        if (killFlag.load(std::memory_order_relaxed))
            break;
        co_await purgeChunks();
        timer.expires_after(std::chrono::milliseconds(VARS::PURGE_DELAY));
        co_await timer.async_wait(asio::use_awaitable);
    }
    co_return;
}

asio::awaitable<void> mainLoop() {
    const auto exec = co_await asio::this_coro::executor;

    // create thread pool with cores-1 threads
    asio::thread_pool cpuPool(std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1));

    // init Redis connection pool
    redis::config cfg;
    cfg.addr.host = "redis-16216.c15.us-east-1-4.ec2.redns.redis-cloud.com";
    cfg.addr.port = "16216";
    cfg.username = "default";
    cfg.password = std::getenv("REDIS_PASSWORD");
    cfg.health_check_interval = std::chrono::seconds(10);
    RedisPool redisPool(exec, cfg, CONFIG::REDIS_CONNECTIONS / 2);
    redis::logger lg(boost::redis::logger::level::emerg);
    redis::connection redisConn(exec, lg); // separate connection for BRPOP
    redisConn.async_run(cfg, asio::detached);  

    AsyncSemaphore pipelineSem(exec, CONFIG::PIPELINE_LIMIT);
    std::unordered_set<std::string> inPipeline;

    DelayedUpdates delayedUpdates;

    std::cout << "Started" << std::endl;

    for (;;) {
        // if SIGTERM/SIGINT, clear pipeline
        if (killFlag.load(std::memory_order_relaxed)) {
            auto exec = co_await asio::this_coro::executor;
            asio::steady_timer timer(exec);
            std::cout << "Waiting for " << inPipeline.size() << " jobs to finish..." << std::endl;

            // poll until jobs finished
            while (inPipeline.size() > 0) {
                timer.expires_after(std::chrono::milliseconds(100));
                co_await timer.async_wait(asio::use_awaitable);
            }
            
            // empty purge queue
            while (needsPurge.size() > 0) {
                co_await purgeChunks();
                timer.expires_after(std::chrono::milliseconds(VARS::PURGE_DELAY));
                co_await timer.async_wait(asio::use_awaitable);
            }

            // push all delayed updates to queue
            std::cout << "Queuing delayed updates..." << std::endl;
            co_await delayedUpdates.purge(redisConn);
            break;
        }

        // listen for chunks to be pushed to update queue
        std::string chunkId;
        try {
            co_await delayedUpdates.refresh(redisConn);

            redis::request req;
            redis::response<std::optional<std::array<std::string, 2>>> resp;
            req.push("BRPOP", VARS::REDIS_UPDATE_QUEUE_PREFIX, "5");
            co_await redisConn.async_exec(req, resp, asio::use_awaitable);

            const auto& result = std::get<0>(resp).value();
            if (!result.has_value())
                continue;

            chunkId = (*result)[1]; // [0] is queue name, [1] is the value
        } catch (const std::exception& e) {
            std::cerr << "[ex] " << e.what() << "\n";
            continue;
        }

        std::cout << chunkId << std::endl;

        co_await pipelineSem.async_acquire();

        // rare case where chunk id is already in the pipeline
        if (inPipeline.contains(chunkId)) {
            // requeue chunk id
            try {
                redis::request req;
                redis::response<redis::ignore_t> resp;
                req.push("LPUSH", VARS::REDIS_UPDATE_QUEUE_PREFIX, chunkId);
                co_await redisConn.async_exec(req, resp, asio::use_awaitable);
            } catch(const std::exception& e) { 
                std::cerr << "[ex] " << e.what() << "\n";
            }
            
            pipelineSem.release();
            continue;
        }

        // process chunk
        inPipeline.insert(chunkId);
        asio::co_spawn(exec, processChunk(
            redisPool,
            cpuPool, 
            delayedUpdates,
            pipelineSem,
            inPipeline,
            std::move(chunkId)
        ), asio::detached);
    }

    cpuPool.join();

    std::cout << "Finished" << std::endl;

    co_return;
}

int main() {
    char* envc = std::getenv("ENV");
    std::string env = envc ? envc : "";
    if (env != "PROD")
        Utils::loadENV(".env");

    Aws::SDKOptions s3Opts;
    Aws::InitAPI(s3Opts);
    cfCli = std::make_shared<CFAsyncClient>(
        "https://1534f5e1cce37d41a018df4c9716751e.r2.cloudflarestorage.com",
        std::getenv("CF_R2_ACCESS_KEY"),
        std::getenv("CF_R2_SECRET_KEY"),
        std::getenv("CF_API_TOKEN"),
        CONFIG::R2_CONNECTIONS,
        true, // enable cache
        CONFIG::R2_CACHE_SIZE * (1<<5)
    );

    assert(CONFIG::PIPELINE_LIMIT > 1 && "Pipeline limit must be greater than 1");

    // coroutine scheduler
    asio::io_context ioc;

    // handle sigint sigterm
    asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([](const boost::system::error_code& ec, int) {
        if (!ec) {
            std::cout << "Cleaning up..." << std::endl;
            killFlag.store(true, std::memory_order_relaxed);
        }
    });
    
    // create coroutine for main loop
    asio::co_spawn(ioc, purgeLoop(), asio::detached);
    asio::co_spawn(ioc, mainLoop(), asio::detached);
    ioc.run();

    Aws::ShutdownAPI(s3Opts);
}