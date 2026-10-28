// Q5: Atomicity. A compare-and-swap loop makes a multi-step update
// ("double the value, but cap at LIMIT") appear indivisible.
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

std::atomic<long> value{1};
constexpr long LIMIT = 1'000'000;

void doubleCapped() {
    long cur = value.load();
    long next;
    do {
        next = std::min(cur * 2, LIMIT);
    } while (!value.compare_exchange_weak(cur, next));   // retry if another thread changed it
}

int main() {
    std::vector<std::thread> ts;
    for (int i = 0; i < 30; ++i) ts.emplace_back(doubleCapped);
    for (auto& t : ts) t.join();
    std::cout << "Final value (capped): " << value << '\n';
    std::cout << "Lock-free on this platform: " << std::boolalpha << value.is_lock_free() << '\n';
}
