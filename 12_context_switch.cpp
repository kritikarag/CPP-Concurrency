// Q12: Context switches. Sleeping or yielding gives up the CPU, so output interleaves;
// with more busy threads than cores, the OS time-slices them preemptively.
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
