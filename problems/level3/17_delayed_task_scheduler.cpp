// Delayed / scheduled task executor.
// schedule(task, delay) runs the task after `delay`; tasks run in deadline order.
// Core: a min-heap by deadline + one condition variable with wait_until on the earliest deadline.
// The subtle case: a new task with an EARLIER deadline arrives while the dispatcher is asleep
// waiting for a later one — notify so it wakes and recomputes.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include <queue>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - priority_queue with a custom comparator (earliest first) — BASICS §10
//  - cv.wait_until(lock, time): sleep until a deadline OR a notify — BASICS §9
//  - thread(&Scheduler::dispatchLoop, this): a class starting its own thread — BASICS §8
//  - lock.unlock() / lock.lock() around running the task — BASICS §4

namespace advanced { void run(); }

// A task and the time it should run.
struct ScheduledTask {
    chrono::steady_clock::time_point runAt;
    function<void()> job;
};

// priority_queue puts the LARGEST first, so "greater runAt" must count as "less important"
// to get the EARLIEST task on top.
// A comparator is a small struct whose operator() answers "should a come after b?" (BASICS §10).
struct EarliestFirst {
    bool operator()(const ScheduledTask& a, const ScheduledTask& b) const {
        return a.runAt > b.runAt;
    }
};

class Scheduler {
    priority_queue<ScheduledTask, vector<ScheduledTask>, EarliestFirst> tasks;
    mutex m;
    condition_variable cv;
    bool stopping;
    thread dispatcher;                  // one background thread that runs tasks when they're due

    void dispatchLoop() {
        unique_lock<mutex> lock(m);
        while (true) {
            if (tasks.empty()) {
                if (stopping) return;
                cv.wait(lock);          // nothing scheduled: sleep until schedule() or stop
                continue;
            }
            auto due = tasks.top().runAt;
            if (chrono::steady_clock::now() < due) {
                // Sleep until the earliest task is due. We may wake EARLY if a new task with
                // an even earlier time arrives (schedule() notifies us); the loop re-checks.
                cv.wait_until(lock, due);   // like cv.wait, but also wakes at time `due` (BASICS §9)
                continue;
            }
            ScheduledTask task = tasks.top();
            tasks.pop();
            lock.unlock();              // release while running the task... (BASICS §4)
            task.job();                 // run without the lock so schedule() isn't blocked
            lock.lock();                // ...and take it back before touching `tasks` again
        }
    }

public:
    Scheduler() {
        stopping = false;
        // Start a thread that runs this object's dispatchLoop() (BASICS §8).
        dispatcher = thread(&Scheduler::dispatchLoop, this);
    }

    void schedule(function<void()> job, int delayMs) {
        {
            lock_guard<mutex> lock(m);
            ScheduledTask t;
            t.runAt = chrono::steady_clock::now() + chrono::milliseconds(delayMs);
            t.job = job;
            tasks.push(t);
        }
        cv.notify_one();                // wake the dispatcher: this task might be the earliest now
    }

    ~Scheduler() {
        {
            lock_guard<mutex> lock(m);
            stopping = true;            // dispatcher finishes everything already scheduled
        }
        cv.notify_one();
        dispatcher.join();
    }
};

// ---------- test ----------
mutex orderMutex;
string order;
void taskA() { lock_guard<mutex> lock(orderMutex); order += 'A'; }
void taskB() { lock_guard<mutex> lock(orderMutex); order += 'B'; }
void taskC() { lock_guard<mutex> lock(orderMutex); order += 'C'; }

int main() {
    {
        Scheduler s;
        s.schedule(taskC, 300);
        s.schedule(taskA, 100);         // scheduled later but due EARLIER than C
        this_thread::sleep_for(chrono::milliseconds(20));
        s.schedule(taskB, 150);
        this_thread::sleep_for(chrono::milliseconds(400));
    }
    assert(order == "ABC");
    cout << "Order: " << order << "\nSolution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a sequence number so tasks with the same deadline run in FIFO order
//  - lambdas and std::move — BASICS §13, §15

namespace advanced {
class DelayedScheduler {
    using Clock = std::chrono::steady_clock;
    struct Item { Clock::time_point due; uint64_t seq; std::function<void()> fn; };
    struct Later { bool operator()(const Item& a, const Item& b) const {
        return a.due != b.due ? a.due > b.due : a.seq > b.seq; } };   // ties: FIFO
    std::priority_queue<Item, std::vector<Item>, Later> heap;
    std::mutex m; std::condition_variable cv;
    uint64_t seq = 0; bool stopping = false;
    std::thread dispatcher;

    void run() {
        std::unique_lock lk(m);
        for (;;) {
            if (stopping && heap.empty()) return;
            if (heap.empty()) { cv.wait(lk); continue; }
            auto due = heap.top().due;
            if (Clock::now() < due) { cv.wait_until(lk, due); continue; }   // re-check: maybe an earlier task arrived
            auto fn = std::move(const_cast<Item&>(heap.top()).fn);
            heap.pop();
            lk.unlock();
            fn();                                   // never run user code under the lock
            lk.lock();
        }
    }
public:
    DelayedScheduler() : dispatcher([this] { run(); }) {}
    ~DelayedScheduler() { { std::lock_guard lk(m); stopping = true; } cv.notify_all(); dispatcher.join(); }
    void schedule(std::function<void()> fn, std::chrono::milliseconds delay) {
        { std::lock_guard lk(m); heap.push({Clock::now() + delay, seq++, std::move(fn)}); }
        cv.notify_one();
    }
};

void run() {
    std::mutex om; std::string order;
    auto rec = [&](char c) { return [&, c] { std::lock_guard lk(om); order += c; }; };
    {
        DelayedScheduler s;
        s.schedule(rec('C'), std::chrono::milliseconds(300));
        s.schedule(rec('A'), std::chrono::milliseconds(100));   // earlier deadline arrives later
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        s.schedule(rec('B'), std::chrono::milliseconds(150));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    assert(order == "ABC");
    std::cout << "Order: " << order << "\nSolution 2 OK\n";
}
// Follow-ups: run tasks on a worker pool instead of the dispatcher thread; cancel(id)
// (lazy deletion with a cancelled set); recurring tasks (re-push with next deadline).
}  // namespace advanced
