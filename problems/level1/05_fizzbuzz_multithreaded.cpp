// FizzBuzz Multithreaded (LeetCode 1195)
// Four threads: fizz (divisible by 3 only), buzz (by 5 only), fizzbuzz (by 15), number (neither).
// Together they print 1..n in order.
// Idea: one shared counter; each thread waits until the current number "belongs" to it.

// KEY LEARNING POINTS
//  - Four threads, one counter. Each owns a predicate on the counter ("is this number mine?").
//  - All four share one loop() helper: wait until the number is mine OR we're done, print, advance.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>
#include <vector>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - one condition per thread inside the while; why notify_all (not notify_one) — BASICS §5

namespace advanced { void run(); }

class FizzBuzz {
    int n;
    int current;
    mutex m;
    condition_variable cv;

public:
    FizzBuzz(int n) {
        this->n = n;
        current = 1;
    }

    // printFizz() outputs "fizz".
    void fizz(function<void()> printFizz) {
        while (true) {
            unique_lock<mutex> lock(m);
            while (current <= n && !(current % 3 == 0 && current % 5 != 0)) cv.wait(lock);
            if (current > n) return;
            printFizz();
            current++;
            cv.notify_all();
        }
    }

    // printBuzz() outputs "buzz".
    void buzz(function<void()> printBuzz) {
        while (true) {
            unique_lock<mutex> lock(m);
            while (current <= n && !(current % 5 == 0 && current % 3 != 0)) cv.wait(lock);
            if (current > n) return;
            printBuzz();
            current++;
            cv.notify_all();
        }
    }

    // printFizzBuzz() outputs "fizzbuzz".
    void fizzbuzz(function<void()> printFizzBuzz) {
        while (true) {
            unique_lock<mutex> lock(m);
            while (current <= n && current % 15 != 0) cv.wait(lock);
            if (current > n) return;
            printFizzBuzz();
            current++;
            cv.notify_all();
        }
    }

    // printNumber(x) outputs "x".
    void number(function<void(int)> printNumber) {
        while (true) {
            unique_lock<mutex> lock(m);
            while (current <= n && (current % 3 == 0 || current % 5 == 0)) cv.wait(lock);
            if (current > n) return;
            printNumber(current);
            current++;
            cv.notify_all();      // notify_ALL: only one of the 3 other threads can use the next number
        }
    }
};

// ---------- test ----------
FizzBuzz* fbz;
vector<string> output;
void printFizz()       { output.push_back("fizz"); }
void printBuzz()       { output.push_back("buzz"); }
void printFizzBuzz()   { output.push_back("fizzbuzz"); }
void printNumber(int x){ output.push_back(to_string(x)); }
void runFizz()     { fbz->fizz(printFizz); }
void runBuzz()     { fbz->buzz(printBuzz); }
void runFizzBuzz() { fbz->fizzbuzz(printFizzBuzz); }
void runNumber()   { fbz->number(printNumber); }

int main() {
    fbz = new FizzBuzz(15);
    thread a(runFizz), b(runBuzz), c(runFizzBuzz), d(runNumber);
    a.join(); b.join(); c.join(); d.join();
    assert(output.size() == 15 && output[2] == "fizz" && output[4] == "buzz" && output[14] == "fizzbuzz");
    for (string s : output) cout << s << " ";
    cout << "\nSolution 1 OK\n";
    delete fbz;
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a helper loop() that takes the "is it mine?" test as a function<bool(int)> — BASICS §7, §13

namespace advanced {
class FizzBuzz {
    int n, cur = 1;
    std::mutex m; std::condition_variable cv;
    void loop(const std::function<bool(int)>& mine, const std::function<void(int)>& print) {
        for (;;) {
            std::unique_lock lk(m);
            cv.wait(lk, [&] { return cur > n || mine(cur); });
            if (cur > n) return;
            print(cur++);
            cv.notify_all();
        }
    }
public:
    std::vector<std::string> out;
    explicit FizzBuzz(int n) : n(n) {}
    void fizz()     { loop([](int i) { return i % 3 == 0 && i % 5 != 0; }, [&](int) { out.push_back("fizz"); }); }
    void buzz()     { loop([](int i) { return i % 5 == 0 && i % 3 != 0; }, [&](int) { out.push_back("buzz"); }); }
    void fizzbuzz() { loop([](int i) { return i % 15 == 0; },             [&](int) { out.push_back("fizzbuzz"); }); }
    void number()   { loop([](int i) { return i % 3 != 0 && i % 5 != 0; }, [&](int i) { out.push_back(std::to_string(i)); }); }
};

void run() {
    FizzBuzz fb(15);
    std::thread a(&FizzBuzz::fizz, &fb), b(&FizzBuzz::buzz, &fb), c(&FizzBuzz::fizzbuzz, &fb), d(&FizzBuzz::number, &fb);
    a.join(); b.join(); c.join(); d.join();
    assert(fb.out.size() == 15 && fb.out[14] == "fizzbuzz" && fb.out[2] == "fizz" && fb.out[4] == "buzz");
    for (auto& s : fb.out) std::cout << s << ' ';
    std::cout << "\nSolution 2 OK\n";
}
// Why notify_all and not notify_one? Only one of the four threads can proceed, and notify_one
// might wake the wrong one, which re-sleeps: a lost wakeup and a hang.
}  // namespace advanced
