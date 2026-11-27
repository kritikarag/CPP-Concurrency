// Bounded Blocking Queue (LeetCode 1188) — THE most important problem in this repo.
// enqueue blocks when full, dequeue blocks when empty, size() returns the count.
// Two implementations: (A) mutex + two condition variables, (B) two semaphores + mutex.
// Extras interviewers ask for: close() for shutdown, and a timed try_dequeue.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <atomic>
#include <chrono>
#include <vector>
#include <queue>
#include <deque>
#include <optional>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - two condition variables: one for "not full", one for "not empty" — BASICS §5
//  - notify_one is enough here: any one waiting consumer (or producer) can make progress

namespace advanced { void run(); }

// Same shape as LeetCode 1188.
class BoundedBlockingQueue {
    queue<int> q;
    int capacity;
    mutex m;
    condition_variable notFull;     // producers wait here when the queue is full
    condition_variable notEmpty;    // consumers wait here when the queue is empty

public:
    BoundedBlockingQueue(int capacity) {
        this->capacity = capacity;
    }

    void enqueue(int element) {
        unique_lock<mutex> lock(m);
        while ((int)q.size() == capacity) notFull.wait(lock);   // full: wait for a consumer
        q.push(element);
        notEmpty.notify_one();                                 // one item added -> wake one consumer
    }

    int dequeue() {
        unique_lock<mutex> lock(m);
        while (q.empty()) notEmpty.wait(lock);                  // empty: wait for a producer
        int value = q.front();
        q.pop();
        notFull.notify_one();                                  // one slot freed -> wake one producer
        return value;
    }

    int size() {
        lock_guard<mutex> lock(m);
        return q.size();
    }
};

// ---------- test ----------
BoundedBlockingQueue bq(5);
atomic<long long> total(0);          // atomic: consumers add to it without a mutex (BASICS §6)
const int ITEMS = 10000;

void producer() {
    for (int i = 1; i <= ITEMS; i++) bq.enqueue(i);
}
void consumer() {
    for (int i = 0; i < ITEMS; i++) total += bq.dequeue();
}

int main() {
    thread p1(producer), p2(producer), c1(consumer), c2(consumer);
    p1.join(); p2.join(); c1.join(); c2.join();
    assert(total == 2LL * ITEMS * (ITEMS + 1) / 2 && bq.size() == 0);
    cout << "Solution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - optional, templates — BASICS §14; std::move — BASICS §15
//  - unlocking BEFORE notify (lock.unlock()) — BASICS §4
//  - wait_for with a timeout — BASICS §9
//  - the same queue built from two semaphores, plus a semaphore built from a mutex + CV — BASICS §16
//  - a close() method so consumers can exit cleanly

namespace advanced {
// ---------- A: condition variables ----------
template <typename T>
class BoundedBlockingQueue {
    std::mutex m;
    std::condition_variable notFull, notEmpty;   // two CVs: wake only the side that can proceed
    std::deque<T> q;
    const size_t cap;
    bool closed = false;
public:
    explicit BoundedBlockingQueue(size_t c) : cap(c) {}

    bool enqueue(T v) {                          // false if the queue was closed
        std::unique_lock lk(m);
        notFull.wait(lk, [&] { return q.size() < cap || closed; });
        if (closed) return false;
        q.push_back(std::move(v));
        lk.unlock();                             // notify after unlocking: woken thread needn't block on m
        notEmpty.notify_one();
        return true;
    }
    std::optional<T> dequeue() {                 // nullopt once closed and drained
        std::unique_lock lk(m);
        notEmpty.wait(lk, [&] { return !q.empty() || closed; });
        if (q.empty()) return std::nullopt;
        T v = std::move(q.front()); q.pop_front();
        lk.unlock();
        notFull.notify_one();
        return v;
    }
    std::optional<T> try_dequeue_for(std::chrono::milliseconds d) {
        std::unique_lock lk(m);
        if (!notEmpty.wait_for(lk, d, [&] { return !q.empty() || closed; }) || q.empty()) return std::nullopt;
        T v = std::move(q.front()); q.pop_front();
        lk.unlock();
        notFull.notify_one();
        return v;
    }
    void close() {
        { std::lock_guard lk(m); closed = true; }
        notFull.notify_all(); notEmpty.notify_all();
    }
    size_t size() { std::lock_guard lk(m); return q.size(); }
};

// ---------- B: semaphores (classic textbook version) ----------
// A counting semaphore built from a mutex + condition variable.
// Why not std::counting_semaphore? Some standard-library versions have had a bug where, under heavy
// contention, a waiting thread can sleep forever (libstdc++ bug 104928). This version is simple,
// obviously correct, and "implement a semaphore" is itself a common interview question.
class Semaphore {
    std::mutex m;
    std::condition_variable cv;
    int permits;
public:
    explicit Semaphore(int initial) : permits(initial) {}
    void acquire() {
        std::unique_lock lk(m);
        cv.wait(lk, [&] { return permits > 0; });   // wait for a free permit...
        --permits;                                  // ...and take it
    }
    void release() {
        { std::lock_guard lk(m); ++permits; }
        cv.notify_one();                            // one permit -> one waiter can proceed
    }
};

template <typename T, int CAP>
class SemaphoreQueue {
    Semaphore emptySlots{CAP}, fullSlots{0};       // free slots / filled slots
    std::mutex m;
    std::deque<T> q;
public:
    void enqueue(T v) {
        emptySlots.acquire();                    // wait for a free slot BEFORE taking the mutex
        { std::lock_guard lk(m); q.push_back(std::move(v)); }
        fullSlots.release();
    }
    T dequeue() {
        fullSlots.acquire();
        T v;
        { std::lock_guard lk(m); v = std::move(q.front()); q.pop_front(); }
        emptySlots.release();
        return v;
    }
};

void run() {
    const int P = 3, C = 3, PER = 10000;
    {
        BoundedBlockingQueue<int> q(8);
        std::atomic<long> sum{0}; std::atomic<int> got{0};
        std::vector<std::thread> prod, cons;
        for (int p = 0; p < P; ++p) prod.emplace_back([&] { for (int i = 1; i <= PER; ++i) q.enqueue(i); });
        for (int c = 0; c < C; ++c) cons.emplace_back([&] { while (auto v = q.dequeue()) { sum += *v; ++got; } });
        for (auto& t : prod) t.join();
        q.close();                                // consumers drain the rest, then exit
        for (auto& t : cons) t.join();
        assert(got == P * PER && sum == (long)P * PER * (PER + 1) / 2);
        assert(!q.try_dequeue_for(std::chrono::milliseconds(10)));
        std::cout << "Solution 2 OK (condition variables): " << got << " items, clean shutdown\n";
    }
    {
        SemaphoreQueue<int, 8> q;
        std::atomic<long> sum{0};
        std::vector<std::thread> ts;
        for (int p = 0; p < P; ++p) ts.emplace_back([&] { for (int i = 1; i <= PER; ++i) q.enqueue(i); });
        for (int c = 0; c < C; ++c) ts.emplace_back([&] { for (int i = 0; i < PER; ++i) sum += q.dequeue(); });
        for (auto& t : ts) t.join();
        assert(sum == (long)P * PER * (PER + 1) / 2);
        std::cout << "Solution 2 OK (semaphores)\n";
    }
}
// Interview questions to be ready for:
//  - Why must wait() take a predicate? (spurious + stolen wakeups)
//  - Why two CVs instead of one? (one CV + notify_one can wake a producer when a consumer was needed)
//  - In B, why acquire the semaphore before the mutex? (reverse order deadlocks when full)
//  - Why bounded at all? (backpressure: a fast producer would otherwise exhaust memory)
}  // namespace advanced
