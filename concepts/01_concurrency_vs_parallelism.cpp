// Q1: Concurrency vs parallelism — two independent tasks on std::thread,
// then the same tasks via std::async (the C++ analogue of a pool/executor).

// KEY LEARNING POINTS
//  - A std::thread starts running the moment it is constructed. join() waits for it to finish.
//  - std::async(std::launch::async, ...) also runs on another thread but hands back a std::future,
//    so you get the return value (or exception) through future.get(). Prefer it for "run and return".
//  - Without std::launch::async, async MAY run the task lazily on the calling thread (deferred).
//  - Concurrency = tasks overlap in time; parallelism = they literally run at once on different cores.
//    On a 1-core machine these two tasks are concurrent but not parallel.
//  - cout is not atomic per line across threads, hence the print mutex.
// NEW IN THIS FILE (explained in BASICS.md):
//  - std::async and future: run a function on another thread and get its result — BASICS §17
//  - volatile (only to stop the compiler deleting the busy loop) — BASICS extras
#include <iostream>
#include <thread>
#include <future>
#include <mutex>

std::mutex printMtx;
void say(const std::string& s) { std::lock_guard<std::mutex> lk(printMtx); std::cout << s << '\n'; }

void task(const std::string& name) {
    say("Starting " + name);
    volatile long sink = 0;   // volatile stops the optimiser deleting the loop
    for (long i = 0; i < 50'000'000; ++i) sink += i;   // simulate CPU work
    say("Completed " + name);
}

int main() {
    std::thread t1(task, "Task 1"), t2(task, "Task 2");
    t1.join(); t2.join();   // MUST join (or detach) before the thread objects die, else std::terminate

    auto f1 = std::async(std::launch::async, task, "Task 1 (async)");
    auto f2 = std::async(std::launch::async, task, "Task 2 (async)");
    f1.get(); f2.get();   // futures wait for completion
    std::cout << "Hardware threads available: " << std::thread::hardware_concurrency() << '\n';
}
