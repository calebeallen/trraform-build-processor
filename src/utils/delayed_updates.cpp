
#include <iostream>

#include "utils/delayed_updates.hpp"

void DelayedUpdates::track(
    const std::string& chunkId, 
    const std::string& childId,
    int64_t delaySeconds
) {
    // create queued entry if it doesn't exist
    if (!_queuedItems.contains(chunkId)) {
        _queue.push({
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count() + delaySeconds,  // FIX: need to add the delay
            chunkId
        });
    }

    // track children that need update
    _queuedItems[chunkId].insert(childId);
}

asio::awaitable<void> DelayedUpdates::refresh(redis::connection& redisConn) {
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    // while now > queue item time stamp
    while (!_queue.empty() && _queue.top().first <= now) {
        const std::string& chunkId = _queue.top().second;
        co_await queueUpdate(redisConn, chunkId);
        _queuedItems.erase(chunkId);
        _queue.pop();
    }
    co_return;
}

asio::awaitable<void> DelayedUpdates::purge(redis::connection& redisConn) {
    for (const auto& [chunkId, _] : _queuedItems)  {
        co_await queueUpdate(redisConn, chunkId);
    }
    co_return;
}

asio::awaitable<void> DelayedUpdates::queueUpdate(
    redis::connection& redisConn, 
    const std::string& chunkId
) {
    static const std::string script = R"(
        local existed = redis.call('EXISTS', KEYS[1])
        local added = redis.call('SADD', KEYS[1], unpack(ARGV, 3, #ARGV))
    
        if existed == 0 and added > 0 then
            redis.call('EXPIRE', KEYS[1], ARGV[1])
            redis.call('LPUSH', KEYS[2], ARGV[2])
        end

        return 0
    )";

    std::vector<std::string> args;
    args.push_back(script);
    args.push_back("2");
    args.push_back(VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX + chunkId); 
    args.push_back(VARS::REDIS_UPDATE_QUEUE_PREFIX);

    // Fixed args first
    args.push_back(VARS::REDIS_EXPIRE);  // ARGV[1]
    args.push_back(chunkId);              // ARGV[2]

    // Variable list last
    auto iter = _queuedItems[chunkId].begin();
    const auto end = _queuedItems[chunkId].end();
    while (iter != end) {
        args.push_back(*iter); 
        ++iter;
    }

    redis::request req;
    redis::response<redis::ignore_t> res;
    req.push_range("EVAL", args);   

    co_await redisConn.async_exec(req, res, asio::use_awaitable);

    co_return;
}