// Q2: Race condition on a shared counter, then two fixes (mutex, atomic).
// Build with -fsanitize=thread to see TSan report the race.

// KEY LEARNING POINTS
//  - ++x is three steps: load x, add 1, store x. Two threads can both load 5 and both store 6:
//    one increment is lost. That is a race condition.
//  - In C++ an unsynchronised write + another access to the same variable is a DATA RACE, which is
//    undefined behaviour — not just a wrong number. The compiler may assume it never happens.
//  - Fix 1: a mutex makes the read-modify-write a critical section (one thread at a time).
//  - Fix 2: std::atomic does it in hardware, without a lock. Best for single variables.
//    memory_order_relaxed is enough for a pure counter: we need atomicity, not ordering with other data.
//  - Atomics don't scale to multi-variable invariants (e.g. "a + b == 100"); use a mutex for those.
// NEW IN THIS FILE (explained in BASICS.md):
//  - atomic and fetch_add — BASICS §6
//  - memory_order_relaxed — BASICS §18
//  - lambdas `[&] { ... }` — BASICS §13
//  - constexpr — BASICS extras
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>

constexpr int N = 1'000'000;

int main() {
    int unsafe = 0;
    auto bad = [&] { for (int i = 0; i < N; ++i) ++unsafe; };   // read-modify-write, not atomic
    std::thread a(bad), b(bad); a.join(); b.join();
    std::cout << "Unsafe:  expected " << 2 * N << ", got " << unsafe << '\n';

    int locked = 0; std::mutex m;
    // Lock taken and released every iteration: correct, but slow under contention.
    auto withMutex = [&] { for (int i = 0; i < N; ++i) { std::lock_guard<std::mutex> lk(m); ++locked; } };
    std::thread c(withMutex), d(withMutex); c.join(); d.join();
    std::cout << "Mutex:   " << locked << '\n';

    std::atomic<int> atomicCount{0};
    auto withAtomic = [&] { for (int i = 0; i < N; ++i) atomicCount.fetch_add(1, std::memory_order_relaxed); };
    std::thread e(withAtomic), f(withAtomic); e.join(); f.join();
    std::cout << "Atomic:  " << atomicCount << '\n';
}
