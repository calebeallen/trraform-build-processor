#include <queue>
#include <unordered_set>
#include <string>

class UniqueQueue {
private:
    std::queue<std::string> _queue;
    std::unordered_set<std::string> _set;

public:
    void push(const std::string& item) {
        if (_set.insert(item).second)
            _queue.push(item);
    }

    void pop() {
        if (!_queue.empty()) {
            _set.erase(_queue.front());
            _queue.pop();
        }
    }

    const std::string& front() const {
        return _queue.front();
    }

    bool empty() const {
        return _queue.empty();
    }

    size_t size() const {
        return _queue.size();
    }
};