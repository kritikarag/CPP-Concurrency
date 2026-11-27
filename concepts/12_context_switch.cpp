// Q12: Context switches. Sleeping or yielding gives up the CPU, so output interleaves;
// with more busy threads than cores, the OS time-slices them preemptively.

// KEY LEARNING POINTS
//  - A context switch saves one thread's registers/stack pointer and restores another's.
//  - Voluntary switches: the thread blocks (sleep, I/O, waiting for a lock). Involuntary: its time
//    slice ends and the OS preempts it.
//  - The direct cost is microseconds; the hidden cost is cold caches/TLB afterwards. That's why
//    thousands of busy threads are slower than a small pool.
//  - The output order here is NOT guaranteed — it depends on the scheduler. Never rely on timing for
//    correctness; use synchronisation.
// NEW IN THIS FILE (explained in BASICS.md):
//  - this_thread::sleep_for — BASICS §9
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>

std::mutex printMtx;

void task(const std::string& name, int delayMs) {
    for (int i = 0; i < 3; ++i) {
        { std::lock_guard<std::mutex> lk(printMtx); std::cout << name << " step " << i << '\n'; }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));   // voluntary switch
    }
    std::lock_guard<std::mutex> lk(printMtx);
    std::cout << name << " completed\n";
}

int main() {
    std::thread t1(task, "Task 1", 50), t2(task, "Task 2", 100);
    t1.join(); t2.join();
}
