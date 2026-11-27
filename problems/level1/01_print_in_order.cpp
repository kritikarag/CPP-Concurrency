// Print in Order (LeetCode 1114)
// Three threads call first(), second(), third() in any order.
// Guarantee the output is always "firstsecondthird".
// Idea: a `turn` counter guarded by a mutex; each call waits for its turn.

// KEY LEARNING POINTS
//  - The OS starts threads in any order; you enforce order with shared state + waiting.
//  - `turn` says whose turn it is (called `stage` in Solution 2). Each method waits until stage == its number, works, then
//    advances stage and wakes everyone.
//  - notify_all, not notify_one: two threads may be waiting, and only one of them is next.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - mutex + condition_variable + `while (...) cv.wait(lock)` — BASICS §2, §4, §5
//  - function<void()>: the print functions LeetCode passes in — BASICS §7
//  - how the test at the bottom works (global object, new/delete, assert) — BASICS §8

namespace advanced { void run(); }

class Foo {
    mutex m;
    condition_variable cv;
    int turn;                       // whose turn it is: 1, 2 or 3

public:
    Foo() {
        turn = 1;
    }

    void first(function<void()> printFirst) {
        unique_lock<mutex> lock(m);
        while (turn != 1) cv.wait(lock);   // sleep until it's my turn (the while handles spurious wakeups)
        printFirst();
        turn = 2;
        cv.notify_all();                   // wake everyone; only the thread whose turn it is will continue
    }

    void second(function<void()> printSecond) {
        unique_lock<mutex> lock(m);
        while (turn != 2) cv.wait(lock);
        printSecond();
        turn = 3;
        cv.notify_all();
    }

    void third(function<void()> printThird) {
        unique_lock<mutex> lock(m);
        while (turn != 3) cv.wait(lock);
        printThird();
        turn = 0;
        cv.notify_all();
    }
};

// ---------- test ----------
Foo* foo;
string output;
void printFirst()  { output += "first"; }
void printSecond() { output += "second"; }
void printThird()  { output += "third"; }
void callFirst()  { foo->first(printFirst); }
void callSecond() { foo->second(printSecond); }
void callThird()  { foo->third(printThird); }

int main() {
    for (int trial = 0; trial < 100; trial++) {
        foo = new Foo();
        output = "";
        thread t3(callThird), t2(callSecond), t1(callFirst);   // started in the "wrong" order on purpose
        t1.join(); t2.join(); t3.join();
        assert(output == "firstsecondthird");
        delete foo;
    }
    cout << "Solution 1 OK: always firstsecondthird\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - lambdas `[&] { ... }` and `cv.wait(lock, predicate)` — BASICS §13
//  - a private helper runAt() so the three methods share one implementation

namespace advanced {
class Foo {
    std::mutex m;
    std::condition_variable cv;
    int stage = 1;
    void runAt(int myStage, const std::function<void()>& work) {
        std::unique_lock lk(m);
        cv.wait(lk, [&] { return stage == myStage; });
        work();
        ++stage;
        cv.notify_all();
    }
public:
    void first(std::function<void()> f)  { runAt(1, f); }
    void second(std::function<void()> f) { runAt(2, f); }
    void third(std::function<void()> f)  { runAt(3, f); }
};

void run() {
    for (int trial = 0; trial < 200; ++trial) {
        Foo foo; std::string out;
        std::thread c([&] { foo.third([&] { out += "third"; }); });
        std::thread b([&] { foo.second([&] { out += "second"; }); });
        std::thread a([&] { foo.first([&] { out += "first"; }); });
        a.join(); b.join(); c.join();
        assert(out == "firstsecondthird");
    }
    std::cout << "Solution 2 OK: 200 trials, always firstsecondthird\n";
}
// Follow-ups: solve with two std::binary_semaphore (s2, s3 start at 0) or two std::promise<void>.
}  // namespace advanced
