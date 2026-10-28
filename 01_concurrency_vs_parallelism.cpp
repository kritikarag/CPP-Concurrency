// Q1: Concurrency vs parallelism — two independent tasks on std::thread,
// then the same tasks via std::async (the C++ analogue of a pool/executor).
#include <iostream>
#include <thread>
#include <future>
#include <mutex>

std::mutex printMtx;
void say(const std::string& s) { std::lock_guard<std::mutex> lk(printMtx); std::cout << s << '\n'; }

void task(const std::string& name) {
    say("Starting " + name);
    volatile long sink = 0;
    for (long i = 0; i < 50'000'000; ++i) sink += i;   // simulate CPU work
    say("Completed " + name);
}

int main() {
    std::thread t1(task, "Task 1"), t2(task, "Task 2");
    t1.join(); t2.join();

    auto f1 = std::async(std::launch::async, task, "Task 1 (async)");
    auto f2 = std::async(std::launch::async, task, "Task 2 (async)");
    f1.get(); f2.get();   // futures wait for completion
    std::cout << "Hardware threads available: " << std::thread::hardware_concurrency() << '\n';
}
