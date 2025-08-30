#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <thread>

#include "thread_pool/thread_pool.hpp"

ThreadPool::ThreadPool(size_t n) {
    _threads.reserve(n);
    for (size_t i = 0; i < n; ++i)
        _threads.emplace_back([this]{ loop(); });
}

ThreadPool::~ThreadPool() {
    bool exp = true;
    if (_running.compare_exchange_strong(exp, false)) {
        _cv.notify_all();
        for (auto& thread : _threads) 
            if (thread.joinable()) 
                thread.join();
    }
}

void ThreadPool::post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(std::move(task));
    }
    _cv.notify_one();
}

void ThreadPool::loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [this]{ 
                return !_running || !_queue.empty(); 
            });
            if (!_running && _queue.empty()) 
                return;
            task = std::move(_queue.front()); 
            _queue.pop();
        }
        task();
    }
}
