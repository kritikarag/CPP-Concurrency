// Reusable (cyclic) barrier.
// N threads call arrive_and_wait(); nobody proceeds until all N arrive, and the barrier
// can be reused round after round. The classic bug: a fast thread races into the next
// round before slow ones leave the current one. The generation counter fixes that.
// (C++20 has std::barrier; interviewers want you to build it.)
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a `round` counter so the same barrier can be reused — read the comment in wait()
//  - an array of atomic<int> in the test — BASICS §6

namespace advanced { void run(); }

class Barrier {
    mutex m;
    condition_variable cv;
    int total;                      // threads that must arrive
    int arrived;                    // threads waiting in the current round
    int round;                      // which round we're in; lets the barrier be reused

public:
    Barrier(int n) {
        total = n;
        arrived = 0;
        round = 0;
    }

    void wait() {
        unique_lock<mutex> lock(m);
        int myRound = round;
        arrived++;
        if (arrived == total) {         // I'm the last one: open the gate
            arrived = 0;
            round++;
            cv.notify_all();
            return;
        }
        // Wait until the round number changes. Waiting on "arrived == total" instead would break:
        // the last thread resets `arrived` to 0, so sleeping threads would never see it equal total.
        while (round == myRound) cv.wait(lock);
    }
};

// ---------- test ----------
const int THREADS = 5, ROUNDS = 300;
Barrier barrier(THREADS);
atomic<int> arrivals[ROUNDS];       // arrivals[r] = how many threads reached round r (atomic: BASICS §6)
void worker() {
    for (int r = 0; r < ROUNDS; r++) {
        arrivals[r]++;
        barrier.wait();
        assert(arrivals[r] == THREADS);     // nobody passes until everyone has arrived
    }
}

int main() {
    vector<thread> threads;
    for (int i = 0; i < THREADS; i++) threads.push_back(thread(worker));
    for (int i = 0; i < THREADS; i++) threads[i].join();
    cout << "Solution 1 OK: " << ROUNDS << " rounds\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - lambdas — BASICS §13

namespace advanced {
class CyclicBarrier {
    std::mutex m; std::condition_variable cv;
    const int parties; int waiting = 0; unsigned long generation = 0;
public:
    explicit CyclicBarrier(int n) : parties(n) {}
    void arrive_and_wait() {
        std::unique_lock lk(m);
        unsigned long gen = generation;
        if (++waiting == parties) {                 // last to arrive opens the gate
            waiting = 0; ++generation;
            cv.notify_all();
            return;
        }
        cv.wait(lk, [&] { return generation != gen; });
    }
};

void run() {
    const int N = 6, ROUNDS = 500;
    CyclicBarrier b(N);
    std::vector<std::atomic<int>> arrivals(ROUNDS);
    std::vector<std::thread> ts;
    for (int t = 0; t < N; ++t) ts.emplace_back([&] {
        for (int r = 0; r < ROUNDS; ++r) {
            ++arrivals[r];
            b.arrive_and_wait();
            assert(arrivals[r] == N);               // after the barrier, everyone has arrived
        }
    });
    for (auto& t : ts) t.join();
    std::cout << "Solution 2 OK: " << ROUNDS << " rounds, no thread ever ran ahead\n";
}
}  // namespace advanced
