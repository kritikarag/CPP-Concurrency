// Q11: A fixed-size thread pool with a task queue and futures for results.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <future>

class ThreadPool {
public:
    explicit ThreadPool(size_t n) {
        for (size_t i = 0; i < n; ++i)
            workers_.emplace_back([this] { workerLoop(); });
    }

    ~ThreadPool() {
        { std::lock_guard<std::mutex> lk(m_); stopping_ = true; }
        cv_.notify_all();
        for (auto& w : workers_) w.join();   // finishes queued tasks first
    }

    template <typename F>
    auto submit(F f) -> std::future<decltype(f())> {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::move(f));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lk(m_);
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

private:
    void workerLoop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty()) return;
                job = std::move(tasks_.front()); tasks_.pop();
            }
            job();   // run outside the lock
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

int main() {
    ThreadPool pool(5);
    std::vector<std::future<int>> results;
    for (int i = 0; i < 10; ++i)
        results.push_back(pool.submit([i] { return i * i; }));
    for (int i = 0; i < 10; ++i)
        std::cout << "Task " << i << " -> " << results[i].get() << '\n';
}
