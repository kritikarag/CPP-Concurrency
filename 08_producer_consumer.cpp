// Q8: Producer-consumer with a bounded blocking queue (mutex + two condition variables).
// Note: one mutex guards the buffer; condition-variable waits use predicates to
// handle spurious wakeups. A close() lets consumers exit cleanly.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>
#include <vector>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t cap) : cap_(cap) {}

    void push(T item) {
        std::unique_lock<std::mutex> lk(m_);
        notFull_.wait(lk, [&] { return q_.size() < cap_; });
        q_.push(std::move(item));
        notEmpty_.notify_one();
    }

    std::optional<T> pop() {   // empty optional = queue closed and drained
        std::unique_lock<std::mutex> lk(m_);
        notEmpty_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (q_.empty()) return std::nullopt;
        T item = std::move(q_.front()); q_.pop();
        notFull_.notify_one();
        return item;
    }

    void close() {
        std::lock_guard<std::mutex> lk(m_);
        closed_ = true;
        notEmpty_.notify_all();
    }

private:
    std::mutex m_;
    std::condition_variable notFull_, notEmpty_;
    std::queue<T> q_;
    size_t cap_;
    bool closed_ = false;
};

std::mutex printMtx;

int main() {
    BoundedQueue<int> buffer(10);

    std::thread producer([&] {
        for (int i = 0; i < 100; ++i) buffer.push(i);
        buffer.close();
    });

    std::vector<std::thread> consumers;
    for (int c = 0; c < 2; ++c) {
        consumers.emplace_back([&, c] {
            int count = 0;
            while (auto item = buffer.pop()) ++count;
            std::lock_guard<std::mutex> lk(printMtx);
            std::cout << "Consumer " << c << " consumed " << count << " items\n";
        });
    }

    producer.join();
    for (auto& t : consumers) t.join();
}
