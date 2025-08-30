#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <thread>

class ThreadPool {

private:
    std::mutex _mutex;
    std::condition_variable _cv;
    std::queue<std::function<void()>> _queue;
    std::vector<std::thread> _threads;
    std::atomic<bool> _running;

    void loop();

public:
    ThreadPool(size_t);
    ~ThreadPool();

    void post(std::function<void()>);

};