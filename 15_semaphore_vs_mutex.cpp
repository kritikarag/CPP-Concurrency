// Q15: Mutex vs semaphore (C++20).
// The mutex protects a counter (exclusive ownership).
// The counting semaphore models a 3-slot resource (a parking lot); any thread may release it.
#include <iostream>
#include <thread>
#include <mutex>
#include <semaphore>
#include <vector>
#include <atomic>
#include <chrono>

int main() {
    // Mutex: exclusive access
    long x = 0; std::mutex m;
    auto inc = [&] { for (int i = 0; i < 1'000'000; ++i) { std::lock_guard<std::mutex> lk(m); ++x; } };
    std::thread a(inc), b(inc); a.join(); b.join();
    std::cout << "Mutex-protected counter: " << x << '\n';

    // Semaphore: at most 3 cars parked at once
    std::counting_semaphore<3> parking(3);
    std::atomic<int> parked{0}, maxParked{0};
    std::vector<std::thread> cars;
    for (int i = 0; i < 10; ++i) {
        cars.emplace_back([&] {
            parking.acquire();
            int now = ++parked;
            int prev = maxParked.load();
            while (now > prev && !maxParked.compare_exchange_weak(prev, now)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            --parked;
            parking.release();
        });
    }
    for (auto& c : cars) c.join();
    std::cout << "Max cars parked at once: " << maxParked << " (limit 3)\n";
}
