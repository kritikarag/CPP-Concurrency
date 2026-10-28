// Q4: Mutual exclusion. Uses lock_guard (RAII) instead of manual lock/unlock,
// so the mutex is released even if the critical section throws.
#include <iostream>
#include <thread>
#include <mutex>

std::mutex resourceMutex;

void process(const char* name) {
    std::lock_guard<std::mutex> lk(resourceMutex);
    std::cout << name << " is accessing the shared resource.\n";
}

int main() {
    std::thread t1(process, "Process A"), t2(process, "Process B");
    t1.join(); t2.join();
}
