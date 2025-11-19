#include "utils/redis_pool.hpp"

#include <boost/asio.hpp>
#include <boost/redis/connection.hpp>

// #include 
RedisPool::RedisPool(const asio::any_io_executor& exec, const redis::config& cfg, size_t poolSize) {
    redis::logger lg(boost::redis::logger::level::emerg);
    
    for (size_t i = 0; i < poolSize; ++i) {
        auto conn = std::make_unique<redis::connection>(exec, lg);
        conn->async_run(cfg, asio::detached);
        _connections.push_back(std::move(conn));
    }
}

redis::connection& RedisPool::get() {
    size_t idx = _next++ % _connections.size();
    return *_connections[idx];
}

redis::connection& RedisPool::operator[](size_t idx) {
    return *_connections[idx % _connections.size()];
}