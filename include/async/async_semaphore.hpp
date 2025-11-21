#pragma once
#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>

namespace asio = boost::asio;

class AsyncSemaphore {
private:
    asio::experimental::channel<void(boost::system::error_code)> _channel;

public:
    explicit AsyncSemaphore(asio::any_io_executor exec, size_t capacity) : _channel(exec, capacity) {
        for (size_t i = 0; i < capacity; ++i) 
            _channel.try_send(boost::system::error_code{});
    }
    
    asio::awaitable<void> async_acquire() {
        co_await _channel.async_receive(asio::use_awaitable);
    }
    
    void release() {
        _channel.try_send(boost::system::error_code{});
    }
};

class AsyncSemaphoreGuard {
private:
    AsyncSemaphore& _sem;
public:
    AsyncSemaphoreGuard(AsyncSemaphore& sem) : _sem(sem) {};
    ~AsyncSemaphoreGuard() { _sem.release(); };
};