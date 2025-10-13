// #include <boost/asio.hpp>
// #include <queue>
// #include <mutex>
// #include <functional>

// namespace asio = boost::asio;

// class counting_semaphore {
//     int count_;
//     std::mutex m_;
//     std::queue<std::function<void()>> waiters_; // thunks that complete acquire()

// public:
//     explicit counting_semaphore(int initial) : count_(initial) {}

//     // co_awaits until a permit is available
//     asio::awaitable<void> acquire() {
//         auto ex = co_await asio::this_coro::executor;

//         // Fast path: take a permit
//         {
//             std::lock_guard lk(m_);
//             if (count_ > 0) { --count_; co_return; }
//         }

//         // Slow path: enqueue a continuation; resume when release() runs
//         co_await asio::async_initiate<asio::use_awaitable_t<>, void()>(
//             [this, ex](auto handler) {
//                 std::lock_guard lk(m_);
//                 // Store a thunk that will resume on the same executor
//                 waiters_.push([ex, h = std::move(handler)]() mutable {
//                     asio::dispatch(ex, [h = std::move(h)]() mutable { std::move(h)(); });
//                 });
//             },
//             asio::use_awaitable);
//     }

//     // signal one waiter or return a permit to the pool
//     void release() {
//         std::function<void()> thunk;
//         {
//             std::lock_guard lk(m_);
//             if (!waiters_.empty()) { thunk = std::move(waiters_.front()); waiters_.pop(); }
//             else { ++count_; return; }
//         }
//         // complete outside the lock
//         thunk();
//     }

//     // optional convenience
//     void release_n(int n) { while (n-- > 0) release(); }
// };

// // simple RAII guard; release() is synchronous, so destructor is safe
// struct permit_guard {
//     counting_semaphore& sem;
//     bool held{false};
//     explicit permit_guard(counting_semaphore& s) : sem(s) {}
//     asio::awaitable<void> acquire() { co_await sem.acquire(); held = true; }
//     ~permit_guard(){ if (held) sem.release(); }
// };