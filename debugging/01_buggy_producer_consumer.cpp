// DEBUGGING DRILL 1 — 4 bugs. Timebox: 20 minutes.
// A bounded queue shared by 2 producers and 2 consumers. It sometimes loses items,
// sometimes crashes, and sometimes hangs at shutdown. Find and fix every bug.
// Answers: ANSWERS.md (don't peek).
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>

class Queue {
    std::mutex m;
    std::condition_variable cv;
    std::queue<int> q;
    size_t cap;
    bool closed = false;
public:
    explicit Queue(size_t c) : cap(c) {}
    void push(int v) {
        std::unique_lock lk(m);
        if (q.size() == cap) cv.wait(lk);
        q.push(v);
        cv.notify_one();
    }
    bool pop(int& out) {
        std::unique_lock lk(m);
        if (q.empty() && !closed) cv.wait(lk);
        if (q.empty()) return false;
        out = q.front(); q.pop();
        cv.notify_one();
        return true;
    }
    void close() { closed = true; }
};

int main() {
    Queue q(4);
    std::atomic<long> sum{0};
    std::vector<std::thread> producers, consumers;
    for (int p = 0; p < 2; ++p) producers.emplace_back([&] { for (int i = 1; i <= 10000; ++i) q.push(i); });
    for (int c = 0; c < 2; ++c) consumers.emplace_back([&] { int v; while (q.pop(v)) sum += v; });
    for (auto& t : producers) t.join();
    q.close();
    for (auto& t : consumers) t.join();
    std::cout << "sum = " << sum << " (expected " << 2L * 10000 * 10001 / 2 << ")\n";
}
