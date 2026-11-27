// Thread-safe stack on a linked list, O(1) push and pop.
// Key interview point: separate top() + pop() is racy even when each is locked,
// because another thread can pop between them. So pop() returns the value.
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <optional>
#include <memory>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - lock_guard and why there's no unlock line — BASICS §3
//  - returning bool + an output parameter `int& value` instead of a separate top()
//  - atomic<long long> counters in the test — BASICS §6

namespace advanced { void run(); }

// WHERE IS THE UNLOCK? (read this if lock_guard is new to you)
// `lock_guard<mutex> lock(m);` locks m when it is created, and unlocks m automatically when
// `lock` is destroyed — which happens the moment the function returns, by ANY return statement
// (or if an exception is thrown). So there is no unlock line to write, and lock_guard doesn't
// even have an unlock() method. pop() below has two returns; both unlock automatically.
//
// The same pop() with manual locking would need an unlock before EVERY exit:
//     m.lock();
//     if (items.empty()) { m.unlock(); return false; }   // forget this one -> every thread hangs
//     value = items.back(); items.pop_back();
//     m.unlock();
//     return true;
// This pattern (an object whose constructor acquires and destructor releases) is called RAII.
// More in BASICS.md.
class SafeStack {
    vector<int> items;
    mutex m;

public:
    void push(int value) {
        lock_guard<mutex> lock(m);      // locked from here...
        items.push_back(value);
    }                                   // ...to here: `lock` is destroyed, m is unlocked

    // Returns false if the stack is empty. Check-and-remove happen under ONE lock,
    // so no other thread can pop in between (separate top() + pop() would race).
    bool pop(int& value) {
        lock_guard<mutex> lock(m);
        if (items.empty()) return false;    // unlocks here (lock destroyed on return)
        value = items.back();
        items.pop_back();
        return true;                        // ...or unlocks here
    }

    int size() {
        lock_guard<mutex> lock(m);
        return items.size();
    }
};

// ---------- test ----------
SafeStack stackForTest;
atomic<long long> poppedSum(0);      // atomic: many threads add to it safely without a mutex (BASICS §6)
atomic<int> poppedCount(0);
const int PER_THREAD = 10000;

void pusher() {
    for (int i = 1; i <= PER_THREAD; i++) stackForTest.push(i);
}
void popper() {
    int value;
    while (poppedCount < 4 * PER_THREAD) {
        if (stackForTest.pop(value)) {
            poppedSum += value;
            poppedCount++;
        }
    }
}

int main() {
    vector<thread> threads;
    for (int i = 0; i < 4; i++) threads.push_back(thread(pusher));
    for (int i = 0; i < 4; i++) threads.push_back(thread(popper));
    for (int i = 0; i < (int)threads.size(); i++) threads[i].join();
    assert(poppedSum == 4LL * PER_THREAD * (PER_THREAD + 1) / 2);
    cout << "Solution 1 OK: every value popped exactly once\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a linked list built from unique_ptr nodes — BASICS §15
//  - optional<T> as the return type of pop() — BASICS §14
//  - templates, `mutable mutex` — BASICS §14, extras
//  - doing allocation and deallocation OUTSIDE the lock to keep it short

namespace advanced {
template <typename T>
class ThreadSafeStack {
    struct Node { T value; std::unique_ptr<Node> next; };
    std::unique_ptr<Node> head;
    mutable std::mutex m;
    size_t count = 0;
public:
    void push(T v) {
        auto n = std::make_unique<Node>(Node{std::move(v), nullptr});  // allocate outside the lock
        std::lock_guard lk(m);
        n->next = std::move(head);
        head = std::move(n);
        ++count;
    }
    std::optional<T> pop() {                                           // check-and-remove is one atomic step
        std::unique_ptr<Node> old;
        {
            std::lock_guard lk(m);
            if (!head) return std::nullopt;
            old = std::move(head);
            head = std::move(old->next);
            --count;
        }
        return std::move(old->value);                                  // node freed outside the lock
    }
    size_t size() const { std::lock_guard lk(m); return count; }
};

void run() {
    ThreadSafeStack<int> s;
    const int P = 4, PER = 20000;
    std::atomic<long> popped{0}, sum{0};
    std::vector<std::thread> ts;
    for (int p = 0; p < P; ++p) ts.emplace_back([&, p] { for (int i = 0; i < PER; ++i) s.push(p * PER + i); });
    for (int c = 0; c < P; ++c) ts.emplace_back([&] {
        while (popped < P * PER) if (auto v = s.pop()) { sum += *v; ++popped; }
    });
    for (auto& t : ts) t.join();
    long n = (long)P * PER;
    assert(sum == n * (n - 1) / 2 && s.size() == 0);
    std::cout << "Solution 2 OK: every pushed value popped exactly once\n";
}
// Follow-ups: a lock-free Treiber stack with compare_exchange (and why the ABA problem
// needs tagged pointers or hazard pointers), and a blocking wait_and_pop().
}  // namespace advanced
