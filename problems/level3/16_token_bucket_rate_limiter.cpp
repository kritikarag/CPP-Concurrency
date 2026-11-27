// Thread-safe token-bucket rate limiter.
// Capacity B tokens, refilled at R tokens/sec. tryAcquire() is non-blocking; acquire() blocks.
// Refill is computed lazily from elapsed time — no background thread needed.
// Follow-up: per-user limiting = unordered_map<user, Bucket> sharded by user hash.
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - chrono and steady_clock: measuring elapsed time — BASICS §9
//  - computing refills lazily from elapsed time (no background thread)

namespace advanced { void run(); }

// Token bucket: the bucket holds up to `capacity` tokens and gains `ratePerSecond` tokens every
// second. Each request spends one token. No tokens left -> request rejected.
class RateLimiter {
    mutex m;
    double capacity;
    double ratePerSecond;
    double tokens;
    chrono::steady_clock::time_point lastRefill;    // steady_clock never jumps (unlike wall time)

    // Add the tokens earned since the last refill. Called with the lock held.
    void refill() {
        auto now = chrono::steady_clock::now();
        // now - lastRefill is a duration; duration<double>(...).count() turns it into seconds (BASICS §9)
        double seconds = chrono::duration<double>(now - lastRefill).count();
        tokens = min(capacity, tokens + seconds * ratePerSecond);
        lastRefill = now;
    }

public:
    RateLimiter(double capacity, double ratePerSecond) {
        this->capacity = capacity;
        this->ratePerSecond = ratePerSecond;
        tokens = capacity;                          // start full: allows an initial burst
        lastRefill = chrono::steady_clock::now();
    }

    // Non-blocking: true if allowed.
    bool tryAcquire() {
        lock_guard<mutex> lock(m);
        refill();
        if (tokens < 1) return false;
        tokens -= 1;
        return true;
    }

    // Blocking: keep trying, sleeping a little in between. Never sleep while holding the lock.
    void acquire() {
        while (!tryAcquire()) {
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }
};

// ---------- test ----------
RateLimiter limiter(10, 100);           // burst of 10, then 100 per second
atomic<int> allowed(0);             // atomic: all 8 client threads count into it (BASICS §6)
chrono::steady_clock::time_point testStart;

void client() {
    while (chrono::steady_clock::now() - testStart < chrono::milliseconds(500)) {
        if (limiter.tryAcquire()) allowed++;
    }
}

int main() {
    testStart = chrono::steady_clock::now();
    vector<thread> clients;
    for (int i = 0; i < 8; i++) clients.push_back(thread(client));
    for (int i = 0; i < 8; i++) clients[i].join();
    cout << "Allowed in 0.5 s: " << allowed << " (expected about 10 + 50 = 60)\n";
    assert(allowed >= 50 && allowed <= 70);

    auto start = chrono::steady_clock::now();
    for (int i = 0; i < 20; i++) limiter.acquire();
    auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();
    cout << "20 blocking acquires took " << ms << " ms\nSolution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - computing how long to sleep instead of polling every millisecond

namespace advanced {
class TokenBucket {
    using Clock = std::chrono::steady_clock;          // steady: immune to wall-clock jumps
    std::mutex m;
    const double capacity, ratePerSec;
    double tokens;
    Clock::time_point last;
    void refill() {
        auto now = Clock::now();
        tokens = std::min(capacity, tokens + std::chrono::duration<double>(now - last).count() * ratePerSec);
        last = now;
    }
public:
    TokenBucket(double cap, double rate) : capacity(cap), ratePerSec(rate), tokens(cap), last(Clock::now()) {}
    bool tryAcquire(double n = 1) {
        std::lock_guard lk(m);
        refill();
        if (tokens < n) return false;
        tokens -= n; return true;
    }
    void acquire(double n = 1) {
        for (;;) {
            std::chrono::duration<double> wait;
            {
                std::lock_guard lk(m);
                refill();
                if (tokens >= n) { tokens -= n; return; }
                wait = std::chrono::duration<double>((n - tokens) / ratePerSec);
            }
            std::this_thread::sleep_for(wait);         // sleep WITHOUT holding the lock
        }
    }
};

void run() {
    TokenBucket b(10, 100);                            // burst 10, then 100/sec
    std::atomic<int> allowed{0};
    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) ts.emplace_back([&] {
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
            if (b.tryAcquire()) ++allowed;
    });
    for (auto& t : ts) t.join();
    std::cout << "Allowed in 0.5s: " << allowed << " (expected about 10 + 50 = 60)\n";
    assert(allowed >= 50 && allowed <= 70);

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 30; ++i) b.acquire();          // must wait roughly (30-bucket)/100 s
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "30 blocking acquires took " << ms << " ms\nSolution 2 OK\n";
}
// Compare with: sliding-window log (exact, memory-heavy), fixed window (bursty at edges),
// and a lock-free version storing (tokens, timestamp) packed into one atomic<uint64_t> with CAS.
}  // namespace advanced
