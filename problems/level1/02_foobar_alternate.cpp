// Print FooBar Alternately (LeetCode 1115)
// Thread A prints "foo" n times, thread B prints "bar" n times; output must be foobarfoobar...
// Idea: two binary semaphores passing a baton back and forth.

// KEY LEARNING POINTS
//  - "Baton passing": two semaphores, exactly one of which is available at any time.
//    fooTurn starts at 1 (foo goes first), barTurn at 0. Each thread takes its own baton and
//    hands the other thread its baton.
//  - Semaphores fit strict alternation better than a CV: no predicate, no shared flag.
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
//  - a bool flag `fooTurn` passed back and forth — BASICS §5
//  - `this->n = n` — BASICS §8

namespace advanced { void run(); }

class FooBar {
    int n;
    mutex m;
    condition_variable cv;
    bool fooTurn;                   // true: foo prints next, false: bar prints next

public:
    FooBar(int n) {
        this->n = n;
        fooTurn = true;
    }

    void foo(function<void()> printFoo) {
        for (int i = 0; i < n; i++) {
            unique_lock<mutex> lock(m);
            while (!fooTurn) cv.wait(lock);
            printFoo();
            fooTurn = false;        // hand the turn to bar
            cv.notify_all();
        }
    }

    void bar(function<void()> printBar) {
        for (int i = 0; i < n; i++) {
            unique_lock<mutex> lock(m);
            while (fooTurn) cv.wait(lock);
            printBar();
            fooTurn = true;         // hand the turn back to foo
            cv.notify_all();
        }
    }
};

// ---------- test ----------
FooBar* fb;
string output;
void printFoo() { output += "foo"; }
void printBar() { output += "bar"; }
void runFoo() { fb->foo(printFoo); }
void runBar() { fb->bar(printBar); }

int main() {
    fb = new FooBar(500);
    thread a(runBar), b(runFoo);
    a.join(); b.join();
    string expected;
    for (int i = 0; i < 500; i++) expected += "foobar";
    assert(output == expected);
    delete fb;
    cout << "Solution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - binary_semaphore as a baton: acquire() takes it, release() hands it over — BASICS §16

namespace advanced {
class FooBar {
    int n;
    std::binary_semaphore fooTurn{1}, barTurn{0};
public:
    explicit FooBar(int n) : n(n) {}
    void foo(std::string& out) {
        for (int i = 0; i < n; ++i) { fooTurn.acquire(); out += "foo"; barTurn.release(); }
    }
    void bar(std::string& out) {
        for (int i = 0; i < n; ++i) { barTurn.acquire(); out += "bar"; fooTurn.release(); }
    }
};

void run() {
    const int n = 1000;
    FooBar fb(n); std::string out;
    std::thread a([&] { fb.foo(out); }), b([&] { fb.bar(out); });
    a.join(); b.join();
    std::string expected; for (int i = 0; i < n; ++i) expected += "foobar";
    assert(out == expected);
    std::cout << "Solution 2 OK: " << n << " alternations\n";
}
// Note: semaphore release/acquire gives the happens-before edge, so writing to `out`
// from both threads is race-free. Try the same with a mutex + condition_variable + bool flag.
}  // namespace advanced
