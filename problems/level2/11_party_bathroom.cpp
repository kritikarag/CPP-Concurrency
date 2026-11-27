// The party bathroom problem (Democrats/Republicans, capacity 3, no mixing).
// Two solutions:
//  A. Strict FIFO ticket queue — no starvation, but can leave a slot empty.
//  B. FIFO with a bypass limit K — same-party people may skip ahead of a waiting rival at most
//     K times in a row, then the bathroom drains for the other party. Higher throughput, still fair.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <vector>
#include <queue>
#include <deque>
#include <algorithm>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a ticket system (nextTicket / nowServing) for strict first-come-first-served order
//  - char 'D' / 'R' to store the party

namespace advanced { void run(); }

// Strict FIFO using tickets, like a bank queue: take a number, wait until it is called.
class Bathroom {
    mutex m;
    condition_variable cv;
    int nextTicket;                 // number handed to the next person who arrives
    int nowServing;                 // ticket allowed to try entering now
    int inside;                     // people inside (0..3)
    char partyInside;               // 'D' or 'R' (only meaningful when inside > 0)

public:
    Bathroom() {
        nextTicket = 0;
        nowServing = 0;
        inside = 0;
        partyInside = ' ';
    }

    void enter(char party) {
        unique_lock<mutex> lock(m);
        int myTicket = nextTicket;
        nextTicket++;
        // I can go in when: it's my turn, there's room, and the bathroom is empty or has my party.
        while (myTicket != nowServing || inside == 3 || (inside > 0 && partyInside != party))
            cv.wait(lock);
        inside++;
        partyInside = party;
        nowServing++;               // call the next ticket (maybe same party, can also enter)
        cv.notify_all();
    }

    void leave() {
        unique_lock<mutex> lock(m);
        inside--;
        cv.notify_all();            // a slot opened, or the room emptied for the other party
    }
};

// ---------- test ----------
Bathroom bathroom;
atomic<int> democratsInside(0), republicansInside(0);
void democrat() {
    bathroom.enter('D');
    democratsInside++;
    assert(republicansInside == 0 && democratsInside <= 3);
    this_thread::sleep_for(chrono::milliseconds(1));
    democratsInside--;
    bathroom.leave();
}
void republican() {
    bathroom.enter('R');
    republicansInside++;
    assert(democratsInside == 0 && republicansInside <= 3);
    this_thread::sleep_for(chrono::milliseconds(2));
    republicansInside--;
    bathroom.leave();
}

int main() {
    vector<thread> people;
    for (int i = 0; i < 60; i++) {
        if (i % 3 == 0) people.push_back(thread(republican));
        else people.push_back(thread(democrat));
    }
    for (int i = 0; i < 60; i++) people[i].join();
    cout << "Solution 1 OK: never mixed, never more than 3\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - enum class Party { D, R }: a type with exactly two named values
//  - a deque of tickets; a bypass counter that trades fairness for throughput
//  - templates for one simulate<Bathroom>() test — BASICS §14

namespace advanced {
enum class Party { D, R };

class FifoBathroom {
    static constexpr int CAP = 3;
    std::mutex m; std::condition_variable cv;
    std::deque<uint64_t> queue; uint64_t nextTicket = 0;
    int inside = 0; Party current = Party::D;
public:
    std::atomic<int> maxInside{0};
    void enter(Party p) {
        std::unique_lock lk(m);
        uint64_t t = nextTicket++; queue.push_back(t);
        cv.wait(lk, [&] { return queue.front() == t && inside < CAP && (inside == 0 || current == p); });
        queue.pop_front(); ++inside; current = p;
        maxInside = std::max(maxInside.load(), inside);
        cv.notify_all();                   // next in line may be the same party
    }
    void exit() { std::lock_guard lk(m); --inside; cv.notify_all(); }
    Party party() { std::lock_guard lk(m); return current; }
};

class BypassBathroom {
    static constexpr int CAP = 3, K = 5;
    std::mutex m; std::condition_variable cv;
    int inside = 0; Party current = Party::D;
    int waiting[2] = {0, 0};
    int bypassed = 0;                      // same-party entries since a rival started waiting
public:
    void enter(Party p) {
        int me = (int)p, other = 1 - me;
        std::unique_lock lk(m);
        ++waiting[me];
        cv.wait(lk, [&] {
            if (inside == 0)                       // empty: go unless the turn has passed to a waiting rival
                return waiting[other] == 0 || current == p;
            return current == p && inside < CAP && (waiting[other] == 0 || bypassed < K);
        });
        --waiting[me];
        if (current != p) bypassed = 0;            // party switched: reset the counter
        current = p; ++inside;
        if (waiting[other] > 0) ++bypassed;        // this entry jumped ahead of a waiting rival
        cv.notify_all();
    }
    void exit() {
        std::lock_guard lk(m);
        if (--inside == 0 && bypassed >= K) { current = current == Party::D ? Party::R : Party::D; bypassed = 0; }
        cv.notify_all();
    }
};

template <typename B>
void simulate(const char* name) {
    B b; std::atomic<int> in[2] = {0, 0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 60; ++i) {
        Party p = (i % 3 == 0) ? Party::R : Party::D;
        ts.emplace_back([&, p] {
            b.enter(p);
            int me = (int)p;
            ++in[me];
            assert(in[1 - me] == 0 && in[me] <= 3);          // no mixing, capacity respected
            std::this_thread::sleep_for(std::chrono::milliseconds(1 + me));
            --in[me];
            b.exit();
        });
    }
    for (auto& t : ts) t.join();
    std::cout << "Solution 2 OK: " << name << " — 60 visits, never mixed, never over capacity\n";
}

void run() {
    simulate<FifoBathroom>("strict FIFO");
    simulate<BypassBathroom>("bypass limit");
}
// Discussion: A is the safe default to code in an interview. Mention B as the throughput upgrade
// and explain that the bypass counter bounds how long a rival waits (no starvation).
}  // namespace advanced
