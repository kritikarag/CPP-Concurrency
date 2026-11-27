// Thread pool task executor with three common follow-ups:
//  1. EXCEPTIONS: a throwing task must not kill its worker. std::packaged_task catches the
//     exception and stores it in the future; future.get() rethrows it in the caller.
//  2. TIMEOUTS: C++ cannot safely kill a running thread. So a timeout has two halves:
//       - the caller stops waiting: future.wait_for(timeout)
//       - the task stops working: cooperative cancellation via std::stop_token, which the
//         task checks periodically. submitWithTimeout() arms a deadline that requests stop.
//  3. RECLAIMING THREADS: like Java's ThreadPoolExecutor — keep `core` threads always, grow up
//     to `max` under load, and let extra threads exit after `keepAlive` of idleness.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <stop_token>
#include <string>
#include <vector>
#include <queue>
#include <deque>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - try / catch to stop a throwing task from killing its worker — BASICS §11
//  - thread(&ThreadPool::workerLoop, this) — BASICS §8
//  - atomic<bool> as a "please stop" flag for timeouts — BASICS §6

namespace advanced { void run(); }

// A plain thread pool, then the three follow-ups added one at a time, each marked FOLLOW-UP.
class ThreadPool {
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex m;
    condition_variable cv;
    bool stopping;

    void workerLoop() {
        while (true) {
            function<void()> task;
            {
                unique_lock<mutex> lock(m);
                while (tasks.empty() && !stopping) cv.wait(lock);
                if (tasks.empty() && stopping) return;     // finish queued work before exiting
                task = tasks.front();
                tasks.pop();
            }                                              // lock released here

            // FOLLOW-UP 1: exceptions. If a task throws and we don't catch it, the exception
            // escapes the thread and std::terminate kills the whole program. Catch it here.
            try {
                task();                                    // run the task WITHOUT holding the lock
            } catch (exception& e) {               // any standard exception; e.what() is its message (BASICS §11)
                lock_guard<mutex> lock(m);
                errors.push_back(e.what());
            } catch (...) {                        // anything else that was thrown
                lock_guard<mutex> lock(m);
                errors.push_back("unknown error");
            }
        }
    }

public:
    vector<string> errors;

    ThreadPool(int numThreads) {
        stopping = false;
        for (int i = 0; i < numThreads; i++)
            workers.push_back(thread(&ThreadPool::workerLoop, this));   // run this object's workerLoop() (BASICS §8)
    }

    void submit(function<void()> task) {
        {
            lock_guard<mutex> lock(m);
            tasks.push(task);
        }
        cv.notify_one();
    }

    ~ThreadPool() {
        {
            lock_guard<mutex> lock(m);
            stopping = true;
        }
        cv.notify_all();                                   // wake every sleeping worker so it can exit
        for (int i = 0; i < (int)workers.size(); i++) workers[i].join();
    }
};

// FOLLOW-UP 2: timeouts. You cannot safely kill a thread (it might hold a lock or be half-way
// through writing data). So the task itself checks a "cancelled" flag, and someone sets that
// flag when time runs out.
struct CancelFlag {
    atomic<bool> cancelled{false};     // atomic: one thread sets it, the task thread reads it (BASICS §6)
};

// FOLLOW-UP 3: reclaiming threads. Keep `coreThreads` always; extra threads that sit idle
// longer than `idleTimeout` exit. Shown in Solution 2's Executor (wait_for + live-thread count).

// ---------- test ----------
atomic<int> completed(0);
void normalTask()  { completed++; }
void failingTask() { throw runtime_error("disk full"); }   // a task that fails (BASICS §11)

CancelFlag flag;
atomic<bool> stoppedEarly(false);
void longTask() {
    for (int step = 0; step < 1000; step++) {
        if (flag.cancelled) { stoppedEarly = true; return; }   // check the flag regularly
        this_thread::sleep_for(chrono::milliseconds(5));
    }
}

int main() {
    vector<string> errors;
    {
        ThreadPool pool(4);
        for (int i = 0; i < 20; i++) pool.submit(normalTask);
        pool.submit(failingTask);
        pool.submit(longTask);
        this_thread::sleep_for(chrono::milliseconds(100));
        flag.cancelled = true;                 // "timeout": ask the long task to stop
        this_thread::sleep_for(chrono::milliseconds(20));
        pool.submit(normalTask);               // the pool still works after a task threw
        this_thread::sleep_for(chrono::milliseconds(50));
        errors = pool.errors;
    }                                          // destructor waits for workers
    assert(completed == 21 && errors.size() == 1 && stoppedEarly);
    cout << "Solution 1 OK: exception caught (" << errors[0] << "), long task cancelled\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - future, packaged_task, stop_token — BASICS §17
//  - templates, lambdas, shared_ptr — BASICS §13–15
//  - wait_for(keepAlive) to let idle threads exit — BASICS §9

namespace advanced {
using namespace std::chrono_literals;

class Executor {
    using Clock = std::chrono::steady_clock;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::function<void()>> tasks;
    std::vector<std::thread> threads;                  // finished threads are joined lazily
    const size_t core, max; const std::chrono::milliseconds keepAlive;
    size_t live = 0, idle = 0;
    bool stopping = false;

    // Deadline watcher for timeouts: one thread, a list of (deadline, stop_source).
    std::vector<std::pair<Clock::time_point, std::stop_source>> deadlines;
    std::thread watcher;

    void startWorker() {                               // caller holds m
        ++live;
        threads.emplace_back([this] { workerLoop(); });
    }
    void workerLoop() {
        std::unique_lock lk(m);
        for (;;) {
            ++idle;
            bool gotWork = cv.wait_for(lk, keepAlive, [&] { return stopping || !tasks.empty(); });
            --idle;
            if (!gotWork && live > core) { --live; return; }   // idle too long and above core: reclaim
            if (!gotWork) continue;
            if (tasks.empty()) { --live; return; }             // stopping and drained
            auto job = std::move(tasks.front()); tasks.pop_front();
            lk.unlock();
            job();                                             // exceptions are caught inside packaged_task
            lk.lock();
        }
    }
    void watchLoop() {
        std::unique_lock lk(m);
        while (!stopping) {
            auto now = Clock::now();
            for (auto& [due, src] : deadlines) if (due <= now) src.request_stop();
            std::erase_if(deadlines, [&](auto& d) { return d.first <= now; });
            auto next = deadlines.empty() ? now + 50ms
                        : std::min_element(deadlines.begin(), deadlines.end(),
                            [](auto& a, auto& b) { return a.first < b.first; })->first;
            cv.wait_until(lk, next);
        }
    }
public:
    Executor(size_t core, size_t max, std::chrono::milliseconds keepAlive)
        : core(core), max(max), keepAlive(keepAlive) {
        std::lock_guard lk(m);
        for (size_t i = 0; i < core; ++i) startWorker();
        watcher = std::thread([this] { watchLoop(); });
    }
    ~Executor() {                                      // graceful: finish queued tasks, then exit
        { std::lock_guard lk(m); stopping = true; }
        cv.notify_all();
        watcher.join();
        for (auto& t : threads) t.join();
    }

    template <typename F>
    auto submit(F f) -> std::future<decltype(f())> {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::move(f));
        auto fut = task->get_future();
        {
            std::lock_guard lk(m);
            if (stopping) throw std::runtime_error("executor is shut down");
            tasks.emplace_back([task] { (*task)(); });
            if (idle == 0 && live < max) startWorker();        // grow under load
        }
        cv.notify_one();
        return fut;
    }

    // The task receives a stop_token that is triggered when `timeout` elapses.
    template <typename F>
    auto submitWithTimeout(F f, std::chrono::milliseconds timeout) {
        std::stop_source src;
        { std::lock_guard lk(m); deadlines.emplace_back(Clock::now() + timeout, src); }
        cv.notify_all();                                       // wake watcher to recompute
        return submit([f = std::move(f), tok = src.get_token()] { return f(tok); });
    }

    size_t liveThreads() { std::lock_guard lk(m); return live; }
};

void run() {
    Executor ex(2, 6, 100ms);

    // 1. Results and exceptions
    auto ok = ex.submit([] { return 6 * 7; });
    auto bad = ex.submit([]() -> int { throw std::runtime_error("disk full"); });
    assert(ok.get() == 42);
    try { bad.get(); assert(false); } catch (const std::runtime_error& e) {
        std::cout << "Exception surfaced to caller: " << e.what() << "\n";
    }
    assert(ex.submit([] { return 1; }).get() == 1);            // the worker survived

    // 2. Timeout: caller-side wait_for + task-side cooperative stop
    auto slow = ex.submitWithTimeout([](std::stop_token st) {
        int steps = 0;
        while (!st.stop_requested() && steps < 1000) { std::this_thread::sleep_for(5ms); ++steps; }
        return st.stop_requested() ? std::string("cancelled") : std::string("finished");
    }, 100ms);
    assert(slow.wait_for(20ms) == std::future_status::timeout);  // caller can give up early...
    assert(slow.get() == "cancelled");                           // ...and the task stops itself
    std::cout << "Timed-out task cancelled cooperatively\n";

    // 3. Growth under load, then reclaim back to core
    std::vector<std::future<void>> burst;
    for (int i = 0; i < 12; ++i) burst.push_back(ex.submit([] { std::this_thread::sleep_for(50ms); }));
    std::this_thread::sleep_for(20ms);
    size_t peak = ex.liveThreads();
    for (auto& f : burst) f.get();
    std::this_thread::sleep_for(400ms);                          // > keepAlive
    std::cout << "Threads at peak: " << peak << ", after idling: " << ex.liveThreads() << "\n";
    assert(peak > 2 && peak <= 6 && ex.liveThreads() == 2);
    std::cout << "Solution 2 OK\n";
}
// Discussion points:
//  - Why not std::thread::detach + kill on timeout? There is no safe kill: the thread may hold
//    a lock or be mid-write. Options in order: cooperative stop, run risky work in a separate
//    PROCESS (which can be killed), or replace the stuck worker and abandon it.
//  - Exceptions without futures (fire-and-forget execute()): wrap the call in try/catch inside
//    the worker and route to an error handler; never let it escape the thread (std::terminate).
//  - Rejection policy when the queue is bounded and full: block, throw, drop, or run on caller.
//  - Shutdown: drain (this version) vs shutdownNow() that clears the queue and requests stop.
}  // namespace advanced
