// Q10: Creating threads — function, lambda, member function, and passing by reference.

// KEY LEARNING POINTS
//  - std::thread accepts any callable: a function, a lambda, or a member function (+ object pointer).
//  - Arguments are COPIED into the thread. To pass by reference, wrap with std::ref, and make sure
//    the referenced object outlives the thread.
//  - Every joinable thread must be joined or detached before its destructor runs, else std::terminate.
//    C++20 std::jthread joins automatically in its destructor and supports stop_token cancellation.
//  - detach() is rarely right in interviews: you lose the ability to wait for it or its result.
// NEW IN THIS FILE (explained in BASICS.md):
//  - lambdas — BASICS §13
//  - member-function threads and std::ref — BASICS §1, §8
#include <iostream>
#include <thread>
#include <mutex>
#include <functional>

std::mutex printMtx;
void task(int n) { std::lock_guard<std::mutex> lk(printMtx); std::cout << "Function thread " << n << ", id " << std::this_thread::get_id() << '\n'; }

struct Worker {
    void run(const std::string& msg) { std::lock_guard<std::mutex> lk(printMtx); std::cout << "Member thread: " << msg << '\n'; }
};

int main() {
    std::thread t1(task, 1);
    std::thread t2([] { std::lock_guard<std::mutex> lk(printMtx); std::cout << "Lambda thread\n"; });
    Worker w;
    std::thread t3(&Worker::run, &w, "hello");

    int result = 0;
    std::thread t4([](int& out) { out = 42; }, std::ref(result));   // std::ref, or it copies

    t1.join(); t2.join(); t3.join(); t4.join();   // every joinable thread must be joined or detached
    std::cout << "Result from t4: " << result << '\n';
}
