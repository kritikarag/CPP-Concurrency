// Q7: Livelock. Both threads hold one lock, try the other, and politely back off
// when it's busy. They stay active but can keep retrying in lockstep.
// Bounded to MAX_TRIES so the demo terminates; random backoff is the usual fix.
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <string>

std::mutex lockA, lockB;
constexpr int MAX_TRIES = 20;

void worker(const char* name, std::mutex& first, std::mutex& second, bool randomBackoff) {
    std::mt19937 rng(std::hash<std::string>{}(name));
    for (int attempt = 1; attempt <= MAX_TRIES; ++attempt) {
        std::unique_lock<std::mutex> a(first);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));   // widen the window
        if (second.try_lock()) {
            std::lock_guard<std::mutex> b(second, std::adopt_lock);
            std::cout << name << ": got both locks on attempt " << attempt << '\n';
            return;
        }
        a.unlock();                                                     // back off, retry
        auto wait = randomBackoff ? std::uniform_int_distribution<int>(1, 30)(rng) : 10;
        std::this_thread::sleep_for(std::chrono::milliseconds(wait));
    }
    std::cout << name << ": gave up (livelock)\n";
}

int main(int argc, char** argv) {
    bool fix = argc > 1 && std::string(argv[1]) == "fix";
    std::thread t1(worker, "Worker 1", std::ref(lockA), std::ref(lockB), fix);
    std::thread t2(worker, "Worker 2", std::ref(lockB), std::ref(lockA), fix);
    t1.join(); t2.join();
}
