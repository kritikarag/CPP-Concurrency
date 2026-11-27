// Odd-Even printer: two threads print 1..N in order, one prints odds, the other evens.
// Idea: shared counter + mutex + condition_variable; each thread waits for its parity.

// KEY LEARNING POINTS
//  - One shared counter `next`; a thread may print only when next's parity is its own.
//  - The predicate also checks `next > N` so the waiting thread wakes up and exits at the end
//    — forgetting the exit condition in the predicate is a classic hang.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - the exit check inside the loop: when `current > limit` the thread must return, and must also notify, or the other thread sleeps forever

namespace advanced { void run(); }

class OddEven {
    mutex m;
    condition_variable cv;
    int current;                    // next number to print
    int limit;

public:
    vector<int> printed;

    OddEven(int n) {
        current = 1;
        limit = n;
    }

    void printOdd() {
        while (true) {
            unique_lock<mutex> lock(m);
            while (current <= limit && current % 2 == 0) cv.wait(lock);   // wait while it's even's turn
            if (current > limit) {                                         // finished
                cv.notify_all();
                return;
            }
            printed.push_back(current);
            current++;
            cv.notify_all();
        }
    }

    void printEven() {
        while (true) {
            unique_lock<mutex> lock(m);
            while (current <= limit && current % 2 == 1) cv.wait(lock);   // wait while it's odd's turn
            if (current > limit) {
                cv.notify_all();
                return;
            }
            printed.push_back(current);
            current++;
            cv.notify_all();
        }
    }
};

// ---------- test ----------
OddEven* oe;
void runOdd()  { oe->printOdd(); }
void runEven() { oe->printEven(); }

int main() {
    oe = new OddEven(20);
    thread a(runOdd), b(runEven);
    a.join(); b.join();
    for (int i = 0; i < 20; i++) assert(oe->printed[i] == i + 1);
    for (int x : oe->printed) cout << x << " ";
    cout << "\nSolution 1 OK\n";
    delete oe;
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - lambdas and a worker shared by both threads, passed the parity as an argument — BASICS §13

namespace advanced {
void run() {
    const int N = 20;
    std::mutex m; std::condition_variable cv;
    int next = 1; std::vector<int> out;

    auto worker = [&](int parity) {
        for (;;) {
            std::unique_lock lk(m);
            cv.wait(lk, [&] { return next > N || next % 2 == parity; });
            if (next > N) return;
            out.push_back(next++);
            cv.notify_all();               // the other thread's turn now
        }
    };
    std::thread odd(worker, 1), even(worker, 0);
    odd.join(); even.join();
    for (int i = 0; i < N; ++i) assert(out[i] == i + 1);
    for (int x : out) std::cout << x << ' ';
    std::cout << "\nSolution 2 OK\n";
}
// Generalise: K threads printing 1..N round-robin (thread i prints numbers with n % K == i).
}  // namespace advanced
