// Thread-safe backup dependency graph.
//
// A FULL backup is self-contained. An INCREMENTAL backup only stores changes, so it depends on the
// backup it was taken on top of (its parent). Each backup has an end time and a retention period.
// Implement, thread-safely:
//   addBackup(id, endTime, retention)
//   addDependency(child, parent)       child is an incremental on top of parent
//   findRecoveryChain(id)              backups needed to restore `id`: full backup first, `id` last
//   expireBackups(now)                 delete backups past retention, BUT never one that a remaining
//                                      backup still depends on
// Practise both halves: get the single-threaded logic right first, then make it thread-safe; running
// out of time before the second half is the common way to fail this one.
//
// KEY LEARNING POINTS
//  - The graph is a forest: each backup has at most one parent. Store parent + number of children.
//  - Recovery chain = walk parent links up to the full backup, then reverse.
//  - Refuse dependencies that would form a cycle (walk up from the new parent; if you reach the child,
//    it's a loop).
//  - Expiry cascades: deleting an expired leaf can make its expired parent deletable too. Process a
//    queue of "expired and no children" backups, like Kahn's topological sort (problem 19).
//  - Thread safety: Solution 1 uses one mutex. Lookups are far more common than changes, so
//    Solution 2 lets lookups run in parallel with a reader-writer lock BUILT FROM A MUTEX AND
//    CONDITION VARIABLE (interviews often disallow built-in read/write locks).
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <random>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a tree stored as "parent id" per node, plus a child count — no pointers needed
//  - reverse() from <algorithm> to flip the chain into full-backup-first order
//  - a queue of nodes that just became removable, processed until empty (cascading deletes)
//  - every public method takes the same one mutex: simplest correct answer

namespace advanced { void run(); }

struct Backup {
    long long endTime;
    long long retention;
    int parent;                        // -1 for a full backup
    int children;                      // how many backups depend directly on this one
};

class BackupManager {
    mutex m;
    unordered_map<int, Backup> backups;

    bool isExpired(const Backup& b, long long now) { return b.endTime + b.retention <= now; }

public:
    bool addBackup(int id, long long endTime, long long retention) {
        lock_guard<mutex> lock(m);
        if (backups.count(id)) return false;
        backups[id] = {endTime, retention, -1, 0};
        return true;
    }

    bool addDependency(int child, int parent) {
        lock_guard<mutex> lock(m);
        if (child == parent || !backups.count(child) || !backups.count(parent)) return false;
        if (backups[child].parent != -1) return false;            // already depends on something
        // Walk up from the new parent. If we reach `child`, linking would create a loop.
        for (int cur = parent; cur != -1; cur = backups[cur].parent)
            if (cur == child) return false;
        backups[child].parent = parent;
        backups[parent].children++;
        return true;
    }

    // Empty if the backup doesn't exist.
    vector<int> findRecoveryChain(int id) {
        lock_guard<mutex> lock(m);
        vector<int> chain;
        if (!backups.count(id)) return chain;
        for (int cur = id; cur != -1; cur = backups[cur].parent) chain.push_back(cur);
        reverse(chain.begin(), chain.end());                       // full backup first
        return chain;
    }

    // Returns the ids that were deleted, sorted.
    vector<int> expireBackups(long long now) {
        lock_guard<mutex> lock(m);
        queue<int> removable;
        for (auto& entry : backups)
            if (isExpired(entry.second, now) && entry.second.children == 0) removable.push(entry.first);

        vector<int> removed;
        while (!removable.empty()) {
            int id = removable.front();
            removable.pop();
            int parent = backups[id].parent;
            backups.erase(id);
            removed.push_back(id);
            if (parent != -1) {                                    // the parent lost a dependant...
                Backup& p = backups[parent];
                p.children--;
                if (p.children == 0 && isExpired(p, now)) removable.push(parent);   // ...maybe it can go too
            }
        }
        sort(removed.begin(), removed.end());
        return removed;
    }

    // Test helper: every remaining backup's parent must still exist.
    bool isConsistent() {
        lock_guard<mutex> lock(m);
        for (auto& entry : backups)
            if (entry.second.parent != -1 && !backups.count(entry.second.parent)) return false;
        return true;
    }
};

// ---------- test ----------
// Each writer thread builds chains: every 5th backup is full, the rest build on the previous one.
// Ids are given in increasing order, so every chain must be increasing.
BackupManager* manager;
atomic<long long> clockNow(0);
atomic<bool> writersDone(false);

void writer(int t) {
    int previous = -1;
    for (int i = 0; i < 400; i++) {
        int id = t * 100000 + i;
        manager->addBackup(id, clockNow.load(), 50);
        if (i % 5 != 0 && previous != -1) manager->addDependency(id, previous);
        previous = id;
    }
}
void reader(int seed) {
    mt19937 rng(seed);
    while (!writersDone) {
        int id = (rng() % 4) * 100000 + rng() % 400;
        vector<int> chain = manager->findRecoveryChain(id);
        if (chain.empty()) continue;                              // not added yet, or already expired
        assert(chain.back() == id && chain.size() <= 5);
        for (int k = 1; k < (int)chain.size(); k++) assert(chain[k - 1] < chain[k]);
    }
}
void expirer() {
    while (!writersDone) {
        clockNow += 10;
        manager->expireBackups(clockNow);
        assert(manager->isConsistent());
    }
}

int main() {
    BackupManager b;
    assert(b.addBackup(1, 0, 100));        // full
    assert(b.addBackup(2, 10, 100));       // incremental on 1
    assert(b.addBackup(3, 20, 1000));      // incremental on 2, kept a long time
    assert(b.addBackup(4, 0, 50));         // independent full backup
    assert(b.addDependency(2, 1) && b.addDependency(3, 2));
    assert(!b.addDependency(1, 3));        // would create a loop
    assert(!b.addDependency(3, 4));        // 3 already has a parent
    assert((b.findRecoveryChain(3) == vector<int>{1, 2, 3}));
    assert((b.expireBackups(150) == vector<int>{4}));        // 1 and 2 are expired but 3 needs them
    assert((b.findRecoveryChain(3) == vector<int>{1, 2, 3}));
    assert((b.expireBackups(1100) == vector<int>{1, 2, 3})); // 3 expires, then 2, then 1 cascade
    assert(b.findRecoveryChain(3).empty());
    cout << "Single-threaded logic correct\n";

    manager = new BackupManager();
    vector<thread> threads;
    for (int t = 0; t < 4; t++) threads.push_back(thread(writer, t));
    vector<thread> others;
    others.push_back(thread(reader, 1));
    others.push_back(thread(reader, 2));
    others.push_back(thread(expirer));
    for (int i = 0; i < 4; i++) threads[i].join();
    writersDone = true;
    for (int i = 0; i < 3; i++) others[i].join();
    assert(manager->isConsistent());
    delete manager;
    cout << "Solution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a writer-preferring reader-writer lock written from a mutex + condition variable (as in
//    problem 9), because interviews often disallow built-in read/write locks
//  - RAII guards (ReadGuard / WriteGuard) so every path unlocks, like lock_guard — BASICS §3
//  - findRecoveryChain takes the READ side, so many lookups run at once; changes take the WRITE side

namespace advanced {

class RWLock {
    std::mutex m;
    std::condition_variable cv;
    int readers = 0, waitingWriters = 0;
    bool writing = false;
public:
    void lockRead() {
        std::unique_lock lk(m);
        cv.wait(lk, [&] { return !writing && waitingWriters == 0; });   // writers go first: no writer starvation
        ++readers;
    }
    void unlockRead() {
        std::lock_guard lk(m);
        if (--readers == 0) cv.notify_all();
    }
    void lockWrite() {
        std::unique_lock lk(m);
        ++waitingWriters;
        cv.wait(lk, [&] { return !writing && readers == 0; });
        --waitingWriters;
        writing = true;
    }
    void unlockWrite() {
        std::lock_guard lk(m);
        writing = false;
        cv.notify_all();
    }
};
struct ReadGuard  { RWLock& l; explicit ReadGuard(RWLock& l) : l(l) { l.lockRead(); }   ~ReadGuard()  { l.unlockRead(); } };
struct WriteGuard { RWLock& l; explicit WriteGuard(RWLock& l) : l(l) { l.lockWrite(); } ~WriteGuard() { l.unlockWrite(); } };

class BackupManager {
    RWLock rw_;
    std::unordered_map<int, Backup> backups_;
    static bool expired(const Backup& b, long long now) { return b.endTime + b.retention <= now; }
public:
    std::atomic<int> concurrentReaders{0}, peakReaders{0};      // test instrumentation

    bool addBackup(int id, long long endTime, long long retention) {
        WriteGuard g(rw_);
        return backups_.try_emplace(id, Backup{endTime, retention, -1, 0}).second;
    }
    bool addDependency(int child, int parent) {
        WriteGuard g(rw_);
        auto c = backups_.find(child), p = backups_.find(parent);
        if (child == parent || c == backups_.end() || p == backups_.end() || c->second.parent != -1) return false;
        for (int cur = parent; cur != -1; cur = backups_.at(cur).parent)
            if (cur == child) return false;
        c->second.parent = parent;
        ++p->second.children;
        return true;
    }
    std::vector<int> findRecoveryChain(int id) {
        ReadGuard g(rw_);
        int now = ++concurrentReaders;
        for (int p = peakReaders; now > p && !peakReaders.compare_exchange_weak(p, now);) {}
        std::vector<int> chain;
        // .find()/.at() never modify the map, so many readers may call them at once.
        for (auto it = backups_.find(id); it != backups_.end();
             it = it->second.parent == -1 ? backups_.end() : backups_.find(it->second.parent))
            chain.push_back(it->first);
        std::reverse(chain.begin(), chain.end());
        --concurrentReaders;
        return chain;
    }
    std::vector<int> expireBackups(long long now) {
        WriteGuard g(rw_);
        std::queue<int> q;
        for (auto& [id, b] : backups_) if (expired(b, now) && b.children == 0) q.push(id);
        std::vector<int> removed;
        while (!q.empty()) {
            int id = q.front(); q.pop();
            int parent = backups_.at(id).parent;
            backups_.erase(id);
            removed.push_back(id);
            if (parent != -1) {
                Backup& p = backups_.at(parent);
                if (--p.children == 0 && expired(p, now)) q.push(parent);
            }
        }
        std::sort(removed.begin(), removed.end());
        return removed;
    }
    bool isConsistent() {
        ReadGuard g(rw_);
        return std::all_of(backups_.begin(), backups_.end(), [&](auto& kv) {
            return kv.second.parent == -1 || backups_.count(kv.second.parent); });
    }
};

void run() {
    BackupManager b;
    std::atomic<long long> now{0};
    std::atomic<bool> done{false};
    std::vector<std::thread> writers, others;
    for (int t = 0; t < 4; ++t) writers.emplace_back([&, t] {
        for (int i = 0, prev = -1; i < 400; ++i) {
            int id = t * 100000 + i;
            b.addBackup(id, now.load(), 50);
            if (i % 5 != 0 && prev != -1) b.addDependency(id, prev);
            prev = id;
        }
    });
    for (int r = 0; r < 4; ++r) others.emplace_back([&, r] {
        std::mt19937 rng(r);
        while (!done) {
            int id = (rng() % 4) * 100000 + rng() % 400;
            auto chain = b.findRecoveryChain(id);
            if (chain.empty()) continue;
            assert(chain.back() == id && chain.size() <= 5 && std::is_sorted(chain.begin(), chain.end()));
        }
    });
    others.emplace_back([&] {
        while (!done) { now += 10; b.expireBackups(now); assert(b.isConsistent()); }
    });
    for (auto& t : writers) t.join();
    done = true;
    for (auto& t : others) t.join();
    assert(b.isConsistent());
    std::cout << "Peak readers inside findRecoveryChain at once: " << b.peakReaders << "\nSolution 2 OK\n";
}
}  // namespace advanced

// Interview follow-ups to raise yourself:
//  - Faster expiry: scanning every backup on each call is O(n). Keep a min-heap ordered by
//    (endTime + retention) and only look at backups that have actually expired.
//  - Deleting the data, not just the record: physically removing a backup is slow I/O. Mark it
//    "expiring" under the write lock, do the deletion outside the lock, then remove the record.
//  - Merging chains: long incremental chains make recovery slow; periodically "synthesize" a new full
//    backup from a chain so old links can expire.
//  - Many children: an incremental chain is a line, but one full backup can have many incrementals
//    branching off it (a tree). The children count handles both.
//  - Finer locking: one RW lock per backup tree, so expiring one tree doesn't block lookups in another.
