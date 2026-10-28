// Q2: Race condition on a shared counter, then two fixes (mutex, atomic).
// Build with -fsanitize=thread to see TSan report the race.
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
    auto withMutex = [&] { for (int i = 0; i < N; ++i) { std::lock_guard<std::mutex> lk(m); ++locked; } };
    std::thread c(withMutex), d(withMutex); c.join(); d.join();
    std::cout << "Mutex:   " << locked << '\n';

    std::atomic<int> atomicCount{0};
    auto withAtomic = [&] { for (int i = 0; i < N; ++i) atomicCount.fetch_add(1, std::memory_order_relaxed); };
    std::thread e(withAtomic), f(withAtomic); e.join(); f.join();
    std::cout << "Atomic:  " << atomicCount << '\n';
}
