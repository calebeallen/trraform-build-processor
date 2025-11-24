#pragma once

#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <cstdint>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/redis/connection.hpp>

#include "config/config.hpp"

namespace asio = boost::asio;
namespace redis = boost::redis;

class DelayedUpdates {

private:
    std::priority_queue<
        std::pair<int64_t, std::string>, 
        std::vector<std::pair<int64_t, std::string>>,
        std::greater<std::pair<int64_t, std::string>>
    > _queue;
    std::unordered_map<std::string, std::unordered_set<std::string>> _queuedItems;

    asio::awaitable<void> queueUpdate(redis::connection&, const std::string& chunkId);

public:
    void track(
        const std::string& chunkId, 
        const std::string& childId,
        int64_t delaySeconds
    );

    asio::awaitable<void> refresh(redis::connection& redisConn);
    asio::awaitable<void> purge(redis::connection& redisConn);

};
