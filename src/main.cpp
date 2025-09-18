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

#include "chunk/chunk_data.hpp"
#include "config/config.hpp"
#include "chunk/chunk_data.hpp"
#include "chunk/base_chunk.hpp"
#include "chunk/d_chunk.hpp"
#include "chunk/l_chunk.hpp"
#include "utils/cf_async_client.hpp"
#include "utils/utils.hpp"

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

static std::atomic<bool> kill = false;

asio::awaitable<void> processChunk(
    redis::connection& redisCli,
    std::shared_ptr<CFAsyncClient> cfCli,
    asio::thread_pool& pool, 
    std::shared_ptr<int> inFlight
) {
    InFlightGuard ifg(inFlight);

    try {
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
                
                return {chunkId, unpack(needsUpdate)}
            )";
            redis::request req;
            redis::response<std::optional<std::vector<std::string>>> res;
            req.push("EVAL", script, "1", VARS::REDIS_UPDATE_QUEUE_PREFIX, VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX);
            co_await redisCli.async_exec(req, res, asio::use_awaitable);
            const auto& optRes = std::get<0>(res).value();
            if (!optRes)
                co_return;

            const std::vector<std::string>& result = *optRes;
            chunkId = result[0];

            needsUpdate.reserve(result.size() - 1);
            needsUpdateStrs.reserve(result.size() - 1);
            for (size_t i = 1; i < result.size(); ++i) {
                needsUpdate.push_back(std::stoll(result[i], nullptr, 16));
                needsUpdateStrs.push_back(std::move(result[i]));
            }
                
        }

        const auto chunkIdParts = ChunkData::parseChunkIdStr(chunkId);
        bool isBaseChunk = chunkId[0] == 'l' && std::get<0>(chunkIdParts) == 2;
        std::shared_ptr<ChunkData> chunk;

        if (chunkId[0] != 'l' || isBaseChunk) {

            // make update flag keys
            std::vector<std::string> getFlagsKeys;
            for (const auto &plotId : needsUpdateStrs)
                getFlagsKeys.push_back(VARS::REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + plotId);

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
            std::vector<UpdateFlags> updateflags(needsUpdateStrs.size());
            for (size_t i = 0; i < updateflags.size(); ++i) {
                if (flagStrs[i]) {
                    std::stringstream ss(*flagStrs[i]);
                    std::string token;
                    
                    while (std::getline(ss, token, ' ')) {
                        if (token == VARS::REDIS_FLAG_SET_DEFAULT_JSON)
                            updateflags[i].setDefaultJson = true;
                        else if (token == VARS::REDIS_FLAG_SET_DEFAULT_BUILD)
                            updateflags[i].setDefaultBuild = true;
                        else if (token == VARS::REDIS_FLAG_NO_IMAGE_UPDATE)
                            updateflags[i].noImageUpdate = true;
                    }
                }
            }

            if (isBaseChunk)
                chunk = std::make_shared<BaseChunk>(chunkId, std::move(needsUpdate), std::move(updateflags), cfCli);
            else
                chunk = std::make_shared<DChunk>(chunkId, std::move(needsUpdate), std::move(updateflags), cfCli);

        } else
            chunk = std::make_shared<LChunk>(chunkId, std::move(needsUpdate), cfCli);


        // pipeline
        co_await chunk->prep();

        
        co_return;

        // process chunk on thread pool
        co_await asio::co_spawn(pool.get_executor(), [chunk]() -> asio::awaitable<void> {
            chunk->process();
            co_return;
        }, asio::use_awaitable);

        const auto nextChunkId = co_await chunk->update();
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
                chunkId,
                VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX,
                VARS::REDIS_UPDATE_QUEUE_PREFIX
            );
            co_await redisCli.async_exec(req, res, asio::use_awaitable); 
        }

    } catch (const std::exception& e) {
        std::cerr << "[ex] " << e.what() << "\n";
    }
}


asio::awaitable<void> loop(
    redis::connection& redisCli,
    std::shared_ptr<CFAsyncClient> cfCli,
    asio::thread_pool& pool
) {
    auto exec = co_await asio::this_coro::executor;
    asio::steady_timer timer(exec);
    std::shared_ptr<int> inFlight = std::make_shared<int>(0);

    for (;;) {
        if (kill.load(std::memory_order_relaxed))
            co_return;

        try {
            timer.expires_after(50ms); // short sleep to unblock 
            co_await timer.async_wait(asio::use_awaitable);

            // throttle
            if (*inFlight >= VARS::MAX_INFLIGHT)
                continue;

            int queueSize;
            {
                redis::request req;
                redis::response<long long> resp;
                req.push("LLEN", VARS::REDIS_UPDATE_QUEUE_PREFIX);
                co_await redisCli.async_exec(req, resp, asio::use_awaitable);
                queueSize = std::get<0>(resp).value();
            }

            // sleep if empty queue
            if (queueSize == 0) {
                timer.expires_after(1s);
                co_await timer.async_wait(asio::use_awaitable);
                continue;
            }

            asio::co_spawn(exec, processChunk(redisCli, cfCli, pool, inFlight), asio::detached);

        } catch (const std::exception& e) {
            std::cerr << "[ex] " << e.what() << "\n";
        }
    }

}


int main() {

    char* envc = std::getenv("ENV");
    std::string env = envc ? envc : "";
    if (env != "PROD")
        Utils::loadENV(".env");

    // coroutine scheduler
    asio::io_context ioc;

    // create thread pool with cores-1 threads
    asio::thread_pool pool(std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1));

    // init Cloudflare helper
    std::shared_ptr<CFAsyncClient> cfCli = std::make_shared<CFAsyncClient>(
        "https://1534f5e1cce37d41a018df4c9716751e.r2.cloudflarestorage.com",
        std::getenv("CF_R2_ACCESS_KEY"),
        std::getenv("CF_R2_SECRET_KEY"),
        std::getenv("CF_API_TOKEN")
    );

    // init Redis client
    redis::config cfg;
    cfg.addr.host = "redis-16216.c15.us-east-1-4.ec2.redns.redis-cloud.com";
    cfg.addr.port = "16216";
    cfg.username = "default";
    cfg.password = std::getenv("REDIS_PASSWORD");

    boost::redis::logger lg(boost::redis::logger::level::emerg);
    redis::connection redisCli(ioc);
    redisCli.async_run(cfg, lg, asio::detached);

    // handle sigint sigterm
    asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&ioc](const boost::system::error_code& ec, int signo) {
        if (!ec)
            kill.store(true, std::memory_order_relaxed);
    });

    std::cout << "Starting" << std::endl;
    
    // create coroutine for main loop
    asio::co_spawn(ioc, loop(redisCli, cfCli, pool), asio::detached);

    ioc.run();
    pool.join();

    return 0;

}