// Thread-safe LRU cache.
// Step 1: classic LRU (hash map + doubly linked list), O(1) get/put.
// Step 2: one mutex. Note: shared_mutex does NOT help — get() reorders the list, so every get is a write.
// Step 3: sharding — N independent LRUs, key hashed to a shard. Contention drops ~N times;
//         the trade-off is that eviction is per-shard (approximate global LRU).

// KEY LEARNING POINTS
//  - LRU in O(1): a doubly linked list keeps recency order (front = newest), and a hash map from key
//    to list iterator finds any node instantly. list::splice moves a node to the front in O(1)
//    without invalidating iterators.
//  - get() MODIFIES the list, so every operation is a write: a reader-writer lock buys nothing.
//  - Sharding splits the key space across N caches with N locks: N times less contention, at the cost
//    of LRU being per shard instead of global.
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <functional>
#include <vector>
#include <list>
#include <unordered_map>
#include <optional>
#include <memory>
#include <algorithm>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - list + iterators + splice, unordered_map, pair — BASICS §10
//  - auto — BASICS §13

namespace advanced { void run(); }

// LeetCode 146 + one mutex.
class LRUCache {
    int capacity;
    list<pair<int, int>> items;                              // (key, value), front = most recently used
    // key -> an ITERATOR to that key's node in the list (an iterator is like a pointer to one element,
    // and a list iterator stays valid while other nodes are added or removed). BASICS §10
    unordered_map<int, list<pair<int, int>>::iterator> where;
    mutex m;

public:
    LRUCache(int capacity) {
        this->capacity = capacity;
    }

    int get(int key) {
        lock_guard<mutex> lock(m);              // get changes the order, so it needs the lock too
        if (where.find(key) == where.end()) return -1;
        auto node = where[key];                      // auto = list<pair<int,int>>::iterator (BASICS §13)
        // splice(destination, sourceList, element): move `node` to the front of the same list.
        // No copying, O(1). This is what "mark as most recently used" means.
        items.splice(items.begin(), items, node);
        return node->second;
    }

    void put(int key, int value) {
        lock_guard<mutex> lock(m);
        if (where.find(key) != where.end()) {       // existing key: update and move to front
            auto node = where[key];
            node->second = value;
            items.splice(items.begin(), items, node);
            return;
        }
        if ((int)items.size() == capacity) {        // full: evict the least recently used (back)
            int oldKey = items.back().first;
            where.erase(oldKey);
            items.pop_back();
        }
        items.push_front({key, value});
        where[key] = items.begin();
    }
};

// ---------- test ----------
LRUCache cache(100);
void worker(int id) {
    for (int i = 0; i < 20000; i++) {
        int key = (i * 7 + id) % 300;
        int value = cache.get(key);
        if (value == -1) cache.put(key, key * 10);
        else assert(value == key * 10);
    }
}

int main() {
    LRUCache small(2);
    small.put(1, 1); small.put(2, 2);
    assert(small.get(1) == 1);
    small.put(3, 3);                    // evicts key 2
    assert(small.get(2) == -1);

    vector<thread> threads;
    for (int i = 0; i < 8; i++) threads.push_back(thread(worker, i));
    for (int i = 0; i < 8; i++) threads[i].join();
    cout << "Solution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - templates — BASICS §14; unique_ptr — BASICS §15; hash<K> to pick a shard

namespace advanced {
template <typename K, typename V>
class LRUCache {
    size_t cap;
    std::list<std::pair<K, V>> items;                                  // front = most recent
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> index;
    std::mutex m;
public:
    explicit LRUCache(size_t c) : cap(c) {}
    std::optional<V> get(const K& k) {
        std::lock_guard lk(m);
        auto it = index.find(k);
        if (it == index.end()) return std::nullopt;
        items.splice(items.begin(), items, it->second);                // move to front, O(1)
        return it->second->second;
    }
    void put(const K& k, V v) {
        std::lock_guard lk(m);
        if (auto it = index.find(k); it != index.end()) {
            it->second->second = std::move(v);
            items.splice(items.begin(), items, it->second);
            return;
        }
        if (items.size() == cap) { index.erase(items.back().first); items.pop_back(); }   // evict least recent: erase from map BEFORE popping (the key lives in the node)
        items.emplace_front(k, std::move(v));
        index[k] = items.begin();
    }
    size_t size() { std::lock_guard lk(m); return items.size(); }
};

template <typename K, typename V, size_t SHARDS = 16>
class ShardedLRU {
    std::vector<std::unique_ptr<LRUCache<K, V>>> shards;
    LRUCache<K, V>& shardFor(const K& k) { return *shards[std::hash<K>{}(k) % SHARDS]; }
public:
    explicit ShardedLRU(size_t totalCap) {
        for (size_t i = 0; i < SHARDS; ++i) shards.push_back(std::make_unique<LRUCache<K, V>>(std::max<size_t>(1, totalCap / SHARDS)));
    }
    std::optional<V> get(const K& k) { return shardFor(k).get(k); }
    void put(const K& k, V v) { shardFor(k).put(k, std::move(v)); }
};

void run() {
    LRUCache<int, int> c(2);
    c.put(1, 1); c.put(2, 2); assert(c.get(1) == 1);
    c.put(3, 3); assert(!c.get(2));                                    // 2 was least recent
    assert(c.get(3) == 3);

    LRUCache<int, int> shared(100);
    ShardedLRU<int, int> sharded(1600);
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) ts.emplace_back([&, t] {
        for (int i = 0; i < 20000; ++i) {
            int k = (i * 7 + t) % 300;
            if (auto v = shared.get(k)) assert(*v == k * 10); else shared.put(k, k * 10);
            if (auto v = sharded.get(k)) assert(*v == k * 10); else sharded.put(k, k * 10);
        }
    });
    for (auto& t : ts) t.join();
    assert(shared.size() <= 100);
    std::cout << "Solution 2 OK: single-lock and sharded LRU, values always consistent\n";
}
}  // namespace advanced
