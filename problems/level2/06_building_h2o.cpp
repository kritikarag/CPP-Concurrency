// Building H2O (LeetCode 1117)
// Hydrogen and oxygen threads arrive in any order. They must leave in groups of
// exactly 2 H + 1 O; no thread from the next molecule may bond before the current one is complete.
// Idea: count slots in the current molecule; reset when it reaches 2H+1O.

// KEY LEARNING POINTS
//  - Treat the "current molecule" as a set of open slots: 2 H slots, 1 O slot.
//  - A thread waits until a slot of its kind is free, takes it, and if that completes the molecule,
//    resets the slots so the next molecule can form.
//  - Taking the slot and writing the output in the same critical section keeps the groups intact.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - counting slots in the current group and resetting when the group is complete

namespace advanced { void run(); }

class H2O {
    mutex m;
    condition_variable cv;
    int hydrogenCount;              // H atoms in the molecule being built (0..2)
    int oxygenCount;                // O atoms in the molecule being built (0..1)

    void resetIfMoleculeComplete() {
        if (hydrogenCount == 2 && oxygenCount == 1) {
            hydrogenCount = 0;      // molecule done: open the slots for the next one
            oxygenCount = 0;
        }
    }

public:
    H2O() {
        hydrogenCount = 0;
        oxygenCount = 0;
    }

    void hydrogen(function<void()> releaseHydrogen) {
        unique_lock<mutex> lock(m);
        while (hydrogenCount == 2) cv.wait(lock);   // both H slots taken: wait for the next molecule
        releaseHydrogen();
        hydrogenCount++;
        resetIfMoleculeComplete();
        cv.notify_all();
    }

    void oxygen(function<void()> releaseOxygen) {
        unique_lock<mutex> lock(m);
        while (oxygenCount == 1) cv.wait(lock);     // O slot taken
        releaseOxygen();
        oxygenCount++;
        resetIfMoleculeComplete();
        cv.notify_all();
    }
};

// ---------- test ----------
H2O* water;
string output;
void releaseH() { output += 'H'; }
void releaseO() { output += 'O'; }
void runH() { water->hydrogen(releaseH); }
void runO() { water->oxygen(releaseO); }

int main() {
    water = new H2O();
    vector<thread> threads;
    for (int i = 0; i < 100; i++) {
        threads.push_back(thread(runO));
        threads.push_back(thread(runH));
        threads.push_back(thread(runH));
    }
    for (int i = 0; i < (int)threads.size(); i++) threads[i].join();
    for (int i = 0; i < (int)output.size(); i += 3) {            // every group of 3 = 2 H + 1 O
        int h = 0;
        for (int k = i; k < i + 3; k++) if (output[k] == 'H') h++;
        assert(h == 2);
    }
    cout << "Solution 1 OK: 100 molecules\n";
    delete water;
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - std::count from <algorithm> in the test to count 'H's

namespace advanced {
class H2O {
    std::mutex m; std::condition_variable cv;
    int h = 0, o = 0;                      // atoms bonded into the current molecule
    void finishIfComplete() {
        if (h == 2 && o == 1) { h = 0; o = 0; }
        cv.notify_all();
    }
public:
    std::string out;
    void hydrogen() {
        std::unique_lock lk(m);
        cv.wait(lk, [&] { return h < 2; });
        ++h; out += 'H';
        finishIfComplete();
    }
    void oxygen() {
        std::unique_lock lk(m);
        cv.wait(lk, [&] { return o < 1; });
        ++o; out += 'O';
        finishIfComplete();
    }
};

void run() {
    const int molecules = 300;
    H2O w; std::vector<std::thread> ts;
    for (int i = 0; i < molecules; ++i) {
        ts.emplace_back(&H2O::oxygen, &w);
        ts.emplace_back(&H2O::hydrogen, &w);
        ts.emplace_back(&H2O::hydrogen, &w);
    }
    for (auto& t : ts) t.join();
    for (size_t i = 0; i < w.out.size(); i += 3) {
        std::string g = w.out.substr(i, 3);
        assert(std::count(g.begin(), g.end(), 'H') == 2);
    }
    std::cout << "Solution 2 OK: " << molecules << " molecules, every group of 3 is 2H+1O\n";
}
// Follow-up: the semaphore + barrier solution in The Little Book of Semaphores, section 5.6.
}  // namespace advanced
