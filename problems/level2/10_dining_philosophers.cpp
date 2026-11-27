// Dining Philosophers (LeetCode 1226)
// 5 philosophers, 5 forks; each needs both neighbouring forks to eat. Avoid deadlock and starvation.
// Three standard fixes, each breaking a different Coffman condition:
//  A. Lock ordering: always pick up the lower-numbered fork first (breaks circular wait). <- used here
//  B. Waiter: allow at most 4 to sit at once (counting_semaphore<4>) — someone can always eat.
//  C. std::scoped_lock(left, right): acquire both atomically (breaks hold-and-wait).
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>
#include <algorithm>
#include <cassert>
#include <array>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - m.lock() / m.unlock() by hand. Normally use lock_guard (BASICS §3); here manual calls make the lock ORDER obvious. A lock_guard version is in Solution 2
//  - mutex forks[5]: an array of mutexes, one per fork
//  - thread(philosopher, i) passes i to the function — BASICS §8

namespace advanced { void run(); }

// Same shape as LeetCode 1226.
class DiningPhilosophers {
    mutex forks[5];

public:
    void wantsToEat(int philosopher,
                    function<void()> pickLeftFork, function<void()> pickRightFork,
                    function<void()> eat,
                    function<void()> putLeftFork, function<void()> putRightFork) {
        int left = philosopher;
        int right = (philosopher + 1) % 5;

        // Everyone picks up the LOWER-numbered fork first. Philosopher 4 therefore reaches for
        // fork 0 before fork 4, which breaks the circle where everyone holds one fork and waits.
        int first = min(left, right);
        int second = max(left, right);

        // Manual lock()/unlock() here so the ORDER is easy to see. In real code prefer lock_guard
        // (BASICS §3), which unlocks automatically even if eat() throws.
        forks[first].lock();
        forks[second].lock();
        pickLeftFork(); pickRightFork();
        eat();
        putLeftFork(); putRightFork();
        forks[second].unlock();
        forks[first].unlock();
    }
};

// ---------- test ----------
DiningPhilosophers table;
atomic<int> meals[5];               // one counter per philosopher; atomic so they're thread-safe (BASICS §6)
void nothing() {}
void philosopher(int id) {
    for (int i = 0; i < 5000; i++) {
        table.wantsToEat(id, nothing, nothing, nothing, nothing, nothing);
        meals[id]++;
    }
}

int main() {
    vector<thread> threads;
    for (int i = 0; i < 5; i++) threads.push_back(thread(philosopher, i));
    for (int i = 0; i < 5; i++) threads[i].join();
    for (int i = 0; i < 5; i++) assert(meals[i] == 5000);
    cout << "Solution 1 OK: no deadlock\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - two lock_guards in a row, declared in the chosen order

namespace advanced {
class DiningPhilosophers {
    std::array<std::mutex, 5> forks;
public:
    std::array<std::atomic<int>, 5> meals{};
    void wantsToEat(int id) {
        int left = id, right = (id + 1) % 5;
        int first = std::min(left, right), second = std::max(left, right);
        std::lock_guard a(forks[first]);
        std::lock_guard b(forks[second]);
        ++meals[id];                       // eat
    }
};

void run() {
    DiningPhilosophers dp; const int MEALS = 10000;
    std::vector<std::thread> ts;
    for (int i = 0; i < 5; ++i) ts.emplace_back([&, i] { for (int k = 0; k < MEALS; ++k) dp.wantsToEat(i); });
    for (auto& t : ts) t.join();
    for (auto& m : dp.meals) assert(m == MEALS);
    std::cout << "Solution 2 OK: no deadlock, every philosopher ate " << MEALS << " times\n";
}
// Try it: replace the ordering with plain (left, right) and run — it will eventually hang.
}  // namespace advanced
