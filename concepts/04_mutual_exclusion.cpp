// Q4: Mutual exclusion. Uses lock_guard (RAII) instead of manual lock/unlock,
// so the mutex is released even if the critical section throws.

// KEY LEARNING POINTS
//  - Mutual exclusion: at most one thread inside the critical section at a time.
//  - Always lock through RAII (lock_guard / unique_lock / scoped_lock). The destructor unlocks, so an
//    early return or an exception can't leave the mutex locked forever.
//  - Never call m.lock() / m.unlock() by hand in interview code; it's the #1 source of stuck mutexes.
//  - The lock protects DATA, not code: every access to the shared data must take the same mutex.
// NEW IN THIS FILE (explained in BASICS.md):
//  - lock_guard — BASICS §3
#include <iostream>
#include <thread>
#include <mutex>

std::mutex resourceMutex;

void process(const char* name) {
    std::lock_guard<std::mutex> lk(resourceMutex);   // locked here, unlocked automatically at the closing brace
    std::cout << name << " is accessing the shared resource.\n";
}

int main() {
    std::thread t1(process, "Process A"), t2(process, "Process B");
    t1.join(); t2.join();
}
