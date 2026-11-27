// DEBUGGING DRILL 3 — 4 bugs. Timebox: 25 minutes.
// A thread pool. Symptoms: tasks print wrong ids, the pool runs one task at a time even
// with 4 workers, and the program never exits.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <chrono>

class ThreadPool {
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex m;
    std::condition_variable cv;
    bool stop = false;
public:
    explicit ThreadPool(int n) {
        for (int i = 0; i < n; ++i) workers.emplace_back([this] {
            for (;;) {
                std::unique_lock lk(m);
                cv.wait(lk, [this] { return stop || !tasks.empty(); });
                if (stop) return;
                auto job = tasks.front(); tasks.pop();
                job();
            }
        });
    }
    void submit(std::function<void()> f) {
        std::lock_guard lk(m);
        tasks.push(f);
        cv.notify_one();
    }
    ~ThreadPool() {
        stop = true;
        for (auto& w : workers) w.join();
    }
};

int main() {
    std::mutex out;
    {
        ThreadPool pool(4);
        for (int i = 0; i < 8; ++i)
            pool.submit([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard lk(out);
                std::cout << "task " << i << " done\n";
            });
    }
    std::cout << "all done\n";
}
