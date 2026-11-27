// Q5: Atomicity. A compare-and-swap loop makes a multi-step update
// ("double the value, but cap at LIMIT") appear indivisible.

// KEY LEARNING POINTS
//  - compare_exchange(expected, desired): "if the value is still `expected`, set it to `desired`;
//    otherwise copy the current value into `expected` and fail". All in one indivisible step.
//  - The CAS LOOP pattern: read, compute the new value, try to CAS; if someone changed it meanwhile,
//    `cur` now holds their value, so recompute and retry. This makes any pure function of the old
//    value atomic, without a lock.
//  - compare_exchange_weak may fail spuriously (even if the value matched) but is cheaper in loops;
//    use compare_exchange_strong when you are NOT in a retry loop.
//  - is_lock_free() tells you whether the CPU does this natively or the library hides a lock inside.
// NEW IN THIS FILE (explained in BASICS.md):
//  - compare_exchange_weak in a retry loop — BASICS §18
//  - emplace_back — BASICS extras
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
        next = std::min(cur * 2, LIMIT);   // compute from the value we last saw
    } while (!value.compare_exchange_weak(cur, next));   // retry if another thread changed it
}

int main() {
    std::vector<std::thread> ts;
    for (int i = 0; i < 30; ++i) ts.emplace_back(doubleCapped);
    for (auto& t : ts) t.join();
    std::cout << "Final value (capped): " << value << '\n';
    std::cout << "Lock-free on this platform: " << std::boolalpha << value.is_lock_free() << '\n';
}
