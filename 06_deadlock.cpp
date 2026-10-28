// Q6: Deadlock and its fix.
// Run with argument "deadlock" to see two threads lock in opposite order and hang.
// Default run uses std::scoped_lock, which acquires both mutexes deadlock-free.
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>

std::mutex lock1, lock2;

void method1Bad() {
    std::lock_guard<std::mutex> a(lock1);
    std::cout << "method1: got lock1\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard<std::mutex> b(lock2);              // waits for method2 forever
    std::cout << "method1: got lock2\n";
}
void method2Bad() {
    std::lock_guard<std::mutex> b(lock2);
    std::cout << "method2: got lock2\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard<std::mutex> a(lock1);              // waits for method1 forever
    std::cout << "method2: got lock1\n";
}

// Fix 1: scoped_lock locks both atomically (deadlock-avoidance algorithm).
// Fix 2 (equally valid): always acquire locks in one global order.
void method1Good() { std::scoped_lock both(lock1, lock2); std::cout << "method1: got both\n"; }
void method2Good() { std::scoped_lock both(lock2, lock1); std::cout << "method2: got both\n"; }

int main(int argc, char** argv) {
    bool demoDeadlock = argc > 1 && std::string(argv[1]) == "deadlock";
    std::thread t1(demoDeadlock ? method1Bad : method1Good);
    std::thread t2(demoDeadlock ? method2Bad : method2Good);
    t1.join(); t2.join();
    std::cout << "Done, no deadlock.\n";
}
