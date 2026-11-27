// Concurrent hash map with lock striping.
// One global lock serialises everything; one lock per bucket wastes memory.
// Striping: S locks, bucket b is guarded by lock b % S. Readers use shared locks.
// Resize must take ALL stripe locks (in index order, to avoid deadlock).
#include <iostream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include <vector>
#include <list>
#include <optional>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - vector<mutex>: one lock per bucket (mutexes can't be copied, so the vector is sized once in the constructor and never resized)
//  - pair and list — BASICS §10

namespace advanced { void run(); }

// Simple version: a fixed number of buckets, and EACH BUCKET HAS ITS OWN LOCK.
// Two threads only block each other if their keys land in the same bucket.
// (Growing the table, and sharing one lock between several buckets — "striping" — are in Solution 2.)
const int NUM_BUCKETS = 64;

class ConcurrentMap {
    vector<list<pair<int, int>>> buckets;       // each bucket is a small list of (key, value)
    vector<mutex> locks;                        // locks[i] protects buckets[i]

    int bucketFor(int key) {
        return ((key % NUM_BUCKETS) + NUM_BUCKETS) % NUM_BUCKETS;   // also works for negative keys
    }

public:
    // `: buckets(...), locks(...)` is an initializer list: it builds the members before the body runs.
    // A mutex can't be copied or moved, so the vector of mutexes must be created at its final size here.
    ConcurrentMap() : buckets(NUM_BUCKETS), locks(NUM_BUCKETS) {}

    void put(int key, int value) {
        int b = bucketFor(key);
        lock_guard<mutex> lock(locks[b]);       // lock just this one bucket
        for (auto& entry : buckets[b]) {
            if (entry.first == key) {           // key exists: update
                entry.second = value;
                return;
            }
        }
        buckets[b].push_back({key, value});
    }

    // Returns true and sets `value` if the key exists.
    bool get(int key, int& value) {
        int b = bucketFor(key);
        lock_guard<mutex> lock(locks[b]);
        for (auto& entry : buckets[b]) {
            if (entry.first == key) {
                value = entry.second;
                return true;
            }
        }
        return false;
    }

    int totalSize() {
        int total = 0;
        for (int b = 0; b < NUM_BUCKETS; b++) {  // lock buckets one at a time, in order
            lock_guard<mutex> lock(locks[b]);
            total += buckets[b].size();
        }
        return total;
    }
};

// ---------- test ----------
ConcurrentMap table;
void writer(int id) {
    for (int i = 0; i < 5000; i++) {
        int key = id * 5000 + i;
        table.put(key, key * 2);
        int value;
        assert(table.get(key, value) && value == key * 2);
    }
}

int main() {
    vector<thread> threads;
    for (int i = 0; i < 8; i++) threads.push_back(thread(writer, i));
    for (int i = 0; i < 8; i++) threads[i].join();
    assert(table.totalSize() == 40000);
    cout << "Solution 1 OK: 40000 keys, all present\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - shared_mutex for readers vs writers — BASICS §16
//  - taking ALL locks in index order to resize safely; re-checking the size after locking

namespace advanced {
template <typename K, typename V, size_t STRIPES = 16>
class StripedMap {
    std::vector<std::list<std::pair<K, V>>> buckets;
    mutable std::vector<std::shared_mutex> locks;
    std::atomic<size_t> count{0};
    std::atomic<size_t> nBuckets;                             // read without a lock, so atomic

    size_t bucketOf(const K& k, size_t n) const { return std::hash<K>{}(k) % n; }
    void maybeResize() {
        if (count.load() < nBuckets.load() * 2) return;
        std::vector<std::unique_lock<std::shared_mutex>> all;
        for (auto& l : locks) all.emplace_back(l);            // fixed order: 0..S-1
        if (count.load() < buckets.size() * 2) return;        // someone else resized
        std::vector<std::list<std::pair<K, V>>> nb(buckets.size() * 2);
        for (auto& b : buckets) for (auto& kv : b) nb[bucketOf(kv.first, nb.size())].push_back(std::move(kv));
        buckets.swap(nb);
        nBuckets.store(buckets.size());                       // published while holding every stripe
    }
public:
    StripedMap() : buckets(STRIPES * 4), locks(STRIPES), nBuckets(STRIPES * 4) {}

    std::optional<V> get(const K& k) const {
        for (;;) {
            size_t n = nBuckets.load(); size_t b = bucketOf(k, n);
            std::shared_lock lk(locks[b % STRIPES]);
            if (n != nBuckets.load()) continue;                // resized between reading n and locking
            for (auto& kv : buckets[b]) if (kv.first == k) return kv.second;
            return std::nullopt;
        }
    }
    void put(const K& k, V v) {
        for (;;) {
            size_t n = nBuckets.load(); size_t b = bucketOf(k, n);
            std::unique_lock lk(locks[b % STRIPES]);
            if (n != nBuckets.load()) continue;
            for (auto& kv : buckets[b]) if (kv.first == k) { kv.second = std::move(v); return; }
            buckets[b].emplace_back(k, std::move(v)); ++count;
            break;
        }
        maybeResize();
    }
    size_t size() const { return count; }
};

void run() {
    StripedMap<int, int> m;
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) ts.emplace_back([&, t] {
        for (int i = 0; i < 5000; ++i) {
            int k = t * 5000 + i;
            m.put(k, k * 2);
            auto v = m.get(k); assert(v && *v == k * 2);
            if (i % 7 == 0) m.get(k / 2);
        }
    });
    for (auto& t : ts) t.join();
    assert(m.size() == 40000);
    for (int k = 0; k < 40000; k += 997) assert(*m.get(k) == k * 2);
    std::cout << "Solution 2 OK: 40000 keys across resizes, all present\n";
}
// Why the re-check: between reading nBuckets and locking the stripe, another thread may resize.
// Resizing needs every stripe lock, so once we hold ours, nBuckets can't change under us.
}  // namespace advanced
