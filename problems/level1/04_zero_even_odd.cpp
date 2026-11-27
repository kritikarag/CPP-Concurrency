// Print Zero Even Odd (LeetCode 1116)
// Three threads: zero() prints 0s, even() prints evens, odd() prints odds.
// For n = 5 the output must be 0102030405.
// Idea: three semaphores. zero runs first, then hands off to odd or even, who hands back.

// KEY LEARNING POINTS
//  - Three semaphores = a small state machine: zero -> (odd or even) -> zero -> ...
//  - zero decides who goes next based on i, then hands the baton back to itself via the other thread.
//  - Each thread loops a fixed number of times, so nobody needs a "done" flag.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <functional>
#include <string>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - function<void(int)>: a function that takes an int — BASICS §7
//  - a `turn` variable with three values acting as a tiny state machine

namespace advanced { void run(); }

class ZeroEvenOdd {
    int n;
    mutex m;
    condition_variable cv;
    int turn;                       // 0: zero prints, 1: odd prints, 2: even prints
    int current;                    // the next non-zero number

public:
    ZeroEvenOdd(int n) {
        this->n = n;
        turn = 0;
        current = 1;
    }

    void zero(function<void(int)> printNumber) {
        for (int i = 0; i < n; i++) {
            unique_lock<mutex> lock(m);
            while (turn != 0) cv.wait(lock);
            printNumber(0);
            if (current % 2 == 1) turn = 1;   // next number is odd
            else turn = 2;                    // next number is even
            cv.notify_all();
        }
    }

    void odd(function<void(int)> printNumber) {
        for (int i = 1; i <= n; i += 2) {
            unique_lock<mutex> lock(m);
            while (turn != 1) cv.wait(lock);
            printNumber(current);
            current++;
            turn = 0;                         // back to zero
            cv.notify_all();
        }
    }

    void even(function<void(int)> printNumber) {
        for (int i = 2; i <= n; i += 2) {
            unique_lock<mutex> lock(m);
            while (turn != 2) cv.wait(lock);
            printNumber(current);
            current++;
            turn = 0;
            cv.notify_all();
        }
    }
};

// ---------- test ----------
ZeroEvenOdd* z;
string output;
void printNumber(int x) { output += to_string(x); }
void runZero() { z->zero(printNumber); }
void runOdd()  { z->odd(printNumber); }
void runEven() { z->even(printNumber); }

int main() {
    z = new ZeroEvenOdd(5);
    thread a(runEven), b(runOdd), c(runZero);
    a.join(); b.join(); c.join();
    assert(output == "0102030405");
    cout << output << "\nSolution 1 OK\n";
    delete z;
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - three binary_semaphores instead of a turn variable — BASICS §16

namespace advanced {
class ZeroEvenOdd {
    int n;
    std::binary_semaphore zeroS{1}, oddS{0}, evenS{0};
public:
    explicit ZeroEvenOdd(int n) : n(n) {}
    void zero(std::string& out) {
        for (int i = 1; i <= n; ++i) {
            zeroS.acquire();
            out += '0';
            (i % 2 ? oddS : evenS).release();
        }
    }
    void even(std::string& out) {
        for (int i = 2; i <= n; i += 2) { evenS.acquire(); out += std::to_string(i); zeroS.release(); }
    }
    void odd(std::string& out) {
        for (int i = 1; i <= n; i += 2) { oddS.acquire(); out += std::to_string(i); zeroS.release(); }
    }
};

void run() {
    ZeroEvenOdd z(5); std::string out;
    std::thread a([&] { z.zero(out); }), b([&] { z.even(out); }), c([&] { z.odd(out); });
    a.join(); b.join(); c.join();
    assert(out == "0102030405");
    std::cout << out << "\nSolution 2 OK\n";
}
}  // namespace advanced
