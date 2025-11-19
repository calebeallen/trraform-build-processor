#pragma once

#include <vector>
#include <memory>
#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/redis/config.hpp>
#include <boost/redis/connection.hpp>

namespace asio = boost::asio;
namespace redis = boost::redis;

class RedisPool {
private:
    std::vector<std::unique_ptr<redis::connection>> _connections;
    size_t _next{0};  // <-- Just plain size_t, no atomic

public:
    RedisPool(const asio::any_io_executor& exec, const redis::config& cfg, size_t poolSize);
    
    redis::connection& get();
    redis::connection& operator[](size_t idx);
};