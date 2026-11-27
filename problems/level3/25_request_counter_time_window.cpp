// Thread-safe request counter over a time window.
// A common systems-coding question:
//   "Implement a thread-safe data structure which can keep track of the number of incoming
//    requests grouped by IP address over a time window. Add support for grouping by other
//    attributes such as BrowserAgent."
//
// Typical rules for this kind of round, which both solutions follow:
//  - Build synchronization yourself from basic primitives. No built-in concurrent collections and
//    no ready-made read/write locks. Only std::mutex and std::condition_variable are used here.
//  - Reason out loud about which shared state you touch and why it holds up, ESPECIALLY at startup
//    and shutdown. Solution 2 has a background cleanup thread whose shutdown is the tricky part.
//  - Don't overcomplicate: Solution 1 is one class and one mutex, and is a complete answer.
//
// KEY LEARNING POINTS
//  - Sliding window: keep each group's request timestamps oldest-first and throw away the ones
//    older than (now - window) whenever you touch that group.
//  - Read the clock INSIDE the lock. Then timestamps enter each list in increasing order, which is
//    what lets you drop old ones from the front. If threads stamped requests before locking, a slow
//    thread could append an older time behind a newer one.
//  - Inject the clock (pass in a function that returns "now"). Tests can then control time exactly
//    instead of sleeping and hoping.
//  - Grouping by "other attributes" is just: for each attribute, a separate map from value to its
//    timestamps. Adding an attribute should not mean rewriting the class.
//  - Memory: storing one timestamp per request is exact but grows with traffic. Solution 2 stores a
//    fixed number of per-time-slice counters per group instead, trading exactness for bounded memory.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - deque<long long> as a sliding window: push_back new times, pop_front expired ones — BASICS §10
//  - function<long long()> to pass in a clock, so tests can control time — BASICS §7
//  - one mutex guarding everything: correct and simple; Solution 2 shows how to scale it
//  - erasing a group once it's empty, so idle IPs don't use memory forever

namespace advanced { void run(); }

struct Request {
    string ip;
    string browserAgent;
};

class RequestCounter {
    mutex m;
    long long windowMs;
    function<long long()> clock;            // returns the current time in milliseconds
    // attribute name ("ip", "browser") -> attribute value ("10.0.0.1") -> request times, oldest first
    unordered_map<string, unordered_map<string, deque<long long>>> groups;

    string valueOf(const Request& r, const string& attribute) {
        if (attribute == "ip") return r.ip;
        if (attribute == "browser") return r.browserAgent;
        return "";
    }

    // Remove timestamps that have fallen out of the window. Called with the lock held.
    void dropExpired(deque<long long>& times, long long now) {
        while (!times.empty() && times.front() <= now - windowMs) times.pop_front();
    }

public:
    RequestCounter(long long windowMs, vector<string> attributes, function<long long()> clock) {
        this->windowMs = windowMs;
        this->clock = clock;
        for (string a : attributes) groups[a];   // create the attribute groups once, up front
    }

    void record(const Request& r) {
        lock_guard<mutex> lock(m);
        long long now = clock();                 // read the time INSIDE the lock (see header)
        for (auto& group : groups) {
            const string& attribute = group.first;
            deque<long long>& times = group.second[valueOf(r, attribute)];
            times.push_back(now);
            dropExpired(times, now);
        }
    }

    // How many requests with attribute == value arrived in the last windowMs milliseconds?
    int count(const string& attribute, const string& value) {
        lock_guard<mutex> lock(m);
        long long now = clock();
        auto group = groups.find(attribute);
        if (group == groups.end()) return 0;
        auto entry = group->second.find(value);
        if (entry == group->second.end()) return 0;
        dropExpired(entry->second, now);
        int n = entry->second.size();
        if (n == 0) group->second.erase(entry);  // nothing left: free the memory for this value
        return n;
    }
};

// ---------- test ----------
// A fake clock the test can move forward by hand. atomic because test threads read it while
// the main thread may change it (BASICS §6).
atomic<long long> fakeNow(0);
long long fakeClock() { return fakeNow.load(); }

RequestCounter* shared;
void sendRequests(int id) {
    Request r;
    r.ip = "10.0.0." + to_string(id % 4);            // threads 0 and 4 share an IP, and so on
    r.browserAgent = (id % 2 == 0) ? "Chrome" : "Firefox";
    for (int i = 0; i < 2000; i++) shared->record(r);
}
void readCounts() {                                   // a reader running at the same time
    for (int i = 0; i < 2000; i++) shared->count("ip", "10.0.0.1");
}

int main() {
    // 1. Window logic with a controlled clock (window = 1000 ms).
    RequestCounter counter(1000, {"ip", "browser"}, fakeClock);
    fakeNow = 0;
    counter.record({"1.1.1.1", "Chrome"});
    counter.record({"1.1.1.1", "Chrome"});
    counter.record({"2.2.2.2", "Firefox"});
    fakeNow = 500;
    counter.record({"1.1.1.1", "Firefox"});
    assert(counter.count("ip", "1.1.1.1") == 3);
    assert(counter.count("browser", "Chrome") == 2);
    assert(counter.count("browser", "Firefox") == 2);
    fakeNow = 1001;                                   // the requests at time 0 have expired
    assert(counter.count("ip", "1.1.1.1") == 1);
    assert(counter.count("ip", "2.2.2.2") == 0);
    fakeNow = 1600;                                   // now everything has expired
    assert(counter.count("browser", "Firefox") == 0);
    assert(counter.count("ip", "9.9.9.9") == 0);      // never seen
    cout << "Window logic correct\n";

    // 2. Many threads recording at once, plus a reader.
    fakeNow = 5000;
    shared = new RequestCounter(1000, {"ip", "browser"}, fakeClock);
    vector<thread> threads;
    for (int i = 0; i < 8; i++) threads.push_back(thread(sendRequests, i));
    threads.push_back(thread(readCounts));
    for (int i = 0; i < (int)threads.size(); i++) threads[i].join();
    for (int k = 0; k < 4; k++) assert(shared->count("ip", "10.0.0." + to_string(k)) == 4000);
    assert(shared->count("browser", "Chrome") == 8000 && shared->count("browser", "Firefox") == 8000);
    delete shared;
    cout << "Solution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - BUCKETED counts: the window is cut into B time slices; each group keeps B counters, so memory
//    is O(groups x B) no matter how much traffic arrives. Counts are exact to one slice.
//  - SHARDING: N shards, each with its own mutex; a group's key picks its shard by hash, so threads
//    touching different IPs rarely wait for each other — BASICS §10, GLOSSARY "sharding"
//  - attributes registered as (name, function that extracts the value) — add one without editing the class
//  - a background cleanup thread with a clean, prompt shutdown: stop flag + condition_variable
//    wait_for, so the destructor never waits for a full cleanup interval — BASICS §5, §9

namespace advanced {

class RequestCounter {
public:
    using Extractor = std::function<std::string(const Request&)>;

    RequestCounter(long long windowMs, int slices, int shards,
                   std::vector<std::pair<std::string, Extractor>> attributes,
                   std::function<long long()> clock,
                   std::chrono::milliseconds cleanupEvery = std::chrono::milliseconds(1000))
        : sliceMs_(windowMs / slices), slices_(slices), shards_(shards),
          attributes_(std::move(attributes)), clock_(std::move(clock)) {
        assert(windowMs % slices == 0);
        // Start the cleaner LAST: every member it reads is fully built by now. Starting a thread
        // in the middle of a constructor is a classic startup bug.
        cleaner_ = std::thread([this, cleanupEvery] { cleanupLoop(cleanupEvery); });
    }

    ~RequestCounter() {
        { std::lock_guard lk(stopMu_); stopping_ = true; }
        stopCv_.notify_one();         // wake the cleaner NOW instead of after its next interval
        cleaner_.join();              // after this, nothing else can touch the shards
    }

    void record(const Request& r) {
        long long now = clock_();     // order doesn't matter with buckets, so no need to hold a lock here
        for (auto& [name, extract] : attributes_) {      // attributes_ never changes: safe to read unlocked
            std::string key = name + '\0' + extract(r);
            Shard& s = shardFor(key);
            std::lock_guard lk(s.m);
            Window& w = s.groups.try_emplace(key, slices_).first->second;
            long long slice = now / sliceMs_;
            int i = slice % slices_;
            if (w.sliceId[i] != slice) { w.sliceId[i] = slice; w.count[i] = 0; }   // reuse a stale slot
            ++w.count[i];
        }
    }

    int count(const std::string& attribute, const std::string& value) {
        std::string key = attribute + '\0' + value;
        Shard& s = shardFor(key);
        std::lock_guard lk(s.m);
        auto it = s.groups.find(key);
        return it == s.groups.end() ? 0 : sum(it->second, clock_() / sliceMs_);
    }

    size_t trackedGroups() {           // for tests: how many groups are in memory
        size_t n = 0;
        for (auto& s : shards_) { std::lock_guard lk(s.m); n += s.groups.size(); }
        return n;
    }

private:
    struct Window {
        std::vector<long long> sliceId;   // which time slice each slot currently holds
        std::vector<int> count;
        explicit Window(int slices) : sliceId(slices, -1), count(slices, 0) {}
    };
    struct Shard {
        std::mutex m;
        std::unordered_map<std::string, Window> groups;
    };

    // Slices still inside the window are (currentSlice - slices, currentSlice].
    int sum(const Window& w, long long currentSlice) const {
        int total = 0;
        for (int i = 0; i < slices_; ++i)
            if (w.sliceId[i] > currentSlice - slices_) total += w.count[i];
        return total;
    }

    Shard& shardFor(const std::string& key) { return shards_[std::hash<std::string>{}(key) % shards_.size()]; }

    void cleanupLoop(std::chrono::milliseconds every) {
        std::unique_lock lk(stopMu_);
        while (!stopping_) {
            stopCv_.wait_for(lk, every, [&] { return stopping_; });   // sleeps, but wakes at once on shutdown
            if (stopping_) return;
            lk.unlock();                       // never hold stopMu_ while taking shard locks
            long long currentSlice = clock_() / sliceMs_;
            for (auto& s : shards_) {           // one shard at a time: other shards stay usable
                std::lock_guard g(s.m);
                std::erase_if(s.groups, [&](auto& kv) { return sum(kv.second, currentSlice) == 0; });
            }
            lk.lock();
        }
    }

    const long long sliceMs_;
    const int slices_;
    std::vector<Shard> shards_;              // sized once in the constructor, never resized
    const std::vector<std::pair<std::string, Extractor>> attributes_;
    const std::function<long long()> clock_;

    std::mutex stopMu_;
    std::condition_variable stopCv_;
    bool stopping_ = false;
    std::thread cleaner_;                    // declared LAST so it's constructed after everything it uses
};

void run() {
    using Ex = RequestCounter::Extractor;
    std::vector<std::pair<std::string, Ex>> attrs = {
        {"ip",      [](const Request& r) { return r.ip; }},
        {"browser", [](const Request& r) { return r.browserAgent; }},
        {"subnet",  [](const Request& r) { return r.ip.substr(0, r.ip.rfind('.')); }},   // a new attribute: one line
    };

    {   // Same window checks as Solution 1: 1000 ms window in 10 slices of 100 ms.
        RequestCounter c(1000, 10, 8, attrs, fakeClock, std::chrono::milliseconds(10));
        fakeNow = 0;
        c.record({"1.1.1.1", "Chrome"}); c.record({"1.1.1.1", "Chrome"}); c.record({"2.2.2.2", "Firefox"});
        fakeNow = 500;
        c.record({"1.1.1.1", "Firefox"});
        assert(c.count("ip", "1.1.1.1") == 3 && c.count("browser", "Firefox") == 2);
        assert(c.count("subnet", "1.1.1") == 3);
        fakeNow = 1001;
        assert(c.count("ip", "1.1.1.1") == 1 && c.count("ip", "2.2.2.2") == 0);
        fakeNow = 1600;
        assert(c.count("browser", "Firefox") == 0);
    }   // destructor stops the cleaner promptly here
    std::cout << "Bucketed window logic correct\n";

    {   // Concurrency + cleanup: 8 writers and a reader while the cleaner runs every 5 ms.
        fakeNow = 50000;
        RequestCounter c(1000, 10, 8, attrs, fakeClock, std::chrono::milliseconds(5));
        std::vector<std::thread> ts;
        for (int id = 0; id < 8; ++id) ts.emplace_back([&, id] {
            Request r{"10.0.0." + std::to_string(id % 4), id % 2 ? "Firefox" : "Chrome"};
            for (int i = 0; i < 2000; ++i) c.record(r);
        });
        ts.emplace_back([&] { for (int i = 0; i < 2000; ++i) c.count("ip", "10.0.0.1"); });
        for (auto& t : ts) t.join();
        for (int k = 0; k < 4; ++k) assert(c.count("ip", "10.0.0." + std::to_string(k)) == 4000);
        assert(c.count("subnet", "10.0.0") == 16000);

        fakeNow = 99999;                                   // everything is now outside the window
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (c.trackedGroups() > 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(c.trackedGroups() == 0);                    // the cleaner freed every idle group
        std::cout << "Cleanup freed all idle groups\n";
    }

    auto t0 = std::chrono::steady_clock::now();
    { RequestCounter c(1000, 10, 4, attrs, fakeClock, std::chrono::hours(1)); }   // 1-hour interval...
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    assert(ms < 1000);                                     // ...yet shutdown is immediate
    std::cout << "Shutdown with a 1-hour cleanup interval took " << ms << " ms\nSolution 2 OK\n";
}
}  // namespace advanced

// Interview follow-ups to raise yourself:
//  - Exact vs approximate: Solution 1 is exact but memory grows with traffic; Solution 2 is exact
//    to one slice (here 100 ms) with fixed memory per group. Ask the interviewer which matters.
//  - Consistency across shards: a "total across all IPs" query would lock shards one at a time,
//    so it's not an instant snapshot. Lock all shards in index order if you need one.
//  - Top-K busiest IPs: scan the groups and keep a min-heap of size K; or maintain it incrementally.
//  - Even less locking: make each slice counter a std::atomic<int>. The tricky part is resetting a
//    stale slot without losing increments — compare_exchange on the slice id first.
//  - Clock choice: use steady_clock in production (never jumps); inject it for tests, as here.
//  - Relation to problem 16: a rate limiter is this counter plus "reject when count >= limit".
