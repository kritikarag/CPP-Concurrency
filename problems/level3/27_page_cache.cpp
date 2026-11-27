// Thread-safe page cache / logical memory unit: a CPU requests a page by pageId; if it's in logical
// memory return it, otherwise fetch it from physical memory.
//
// Memory holds at most `capacity` pages. Reading a page that isn't in memory is a PAGE FAULT: load it
// from the slow disk, evicting the least-recently-used page if memory is full.
//
// KEY LEARNING POINTS
//  - Never hold the lock during the slow disk read, or every thread waits for every page fault.
//  - But then two threads can miss the SAME page at once and both load it. Fix: before unlocking,
//    put a placeholder entry marked "loading". A second thread that finds it waits on a condition
//    variable instead of loading again.
//  - A page that's still loading must not be evicted (someone is about to use it). Only fully loaded
//    pages go into the LRU list, so they're the only eviction candidates.
//  - If every frame is mid-load and memory is full, a new miss has nothing to evict: it waits.
//  - Solution 2 adds writes: modified ("dirty") pages must be written back to disk before their frame
//    is reused, and no one may re-read that page from disk until the write-back has finished.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <random>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - an LRU list + map of list positions, same idea as the LRU cache (problem 13) — BASICS §10
//  - unlock -> slow work -> lock again, with a "loading" placeholder so the work isn't duplicated — BASICS §4
//  - `while (true) { ... continue; }`: after waking up, start the checks again from the top,
//    because anything may have changed while we slept
//  - asserting the cache never exceeds its capacity, and the disk never loads one page twice at once

namespace advanced { void run(); }

// The slow "physical memory" / disk. Many threads may call it at once (for different pages).
class Disk {
    vector<string> pages;
    mutex m;
    int latencyMs;
public:
    atomic<int> reads{0}, writes{0};
    vector<atomic<int>> busy;              // test check: operations in progress per page (must stay <= 1)

    Disk(int numPages, int latencyMs) : busy(numPages) {
        this->latencyMs = latencyMs;
        for (int i = 0; i < numPages; i++) pages.push_back("page-" + to_string(i));
    }
    string readPage(int id) {
        assert(++busy[id] == 1);           // nobody else is reading or writing this page right now
        this_thread::sleep_for(chrono::milliseconds(latencyMs));
        reads++;
        string data;
        { lock_guard<mutex> lock(m); data = pages[id]; }
        busy[id]--;
        return data;
    }
    void writePage(int id, string data) {
        assert(++busy[id] == 1);
        this_thread::sleep_for(chrono::milliseconds(latencyMs));
        writes++;
        { lock_guard<mutex> lock(m); pages[id] = data; }
        busy[id]--;
    }
    string peek(int id) { lock_guard<mutex> lock(m); return pages[id]; }   // test helper
};

class PageCache {
    struct Frame {
        string data;
        bool loading;
    };
    mutex m;
    condition_variable pageLoaded;
    int capacity;
    Disk& disk;
    unordered_map<int, Frame> frames;                 // pages in memory, or being loaded into memory
    list<int> lru;                                    // LOADED pages only; front = most recently used
    unordered_map<int, list<int>::iterator> lruPos;

    void markUsed(int pageId) {                       // move to the front of the LRU list
        if (lruPos.count(pageId)) lru.erase(lruPos[pageId]);
        lru.push_front(pageId);
        lruPos[pageId] = lru.begin();
    }

public:
    int hits = 0, misses = 0;                         // read with the lock held, or after all threads finish

    PageCache(int capacity, Disk& disk) : disk(disk) {
        this->capacity = capacity;
    }

    string read(int pageId) {
        unique_lock<mutex> lock(m);
        while (true) {
            auto it = frames.find(pageId);
            if (it != frames.end()) {
                if (it->second.loading) {             // someone else is loading it: wait, don't load twice
                    pageLoaded.wait(lock);
                    continue;
                }
                hits++;
                markUsed(pageId);
                return it->second.data;
            }

            if ((int)frames.size() >= capacity) {     // memory full: evict the least recently used page
                if (lru.empty()) {                    // every frame is still loading: nothing to evict yet
                    pageLoaded.wait(lock);
                    continue;
                }
                int victim = lru.back();
                lru.pop_back();
                lruPos.erase(victim);
                frames.erase(victim);
            }

            misses++;
            frames[pageId].loading = true;            // reserve a frame so other threads wait for us
            assert((int)frames.size() <= capacity);
            lock.unlock();
            string data = disk.readPage(pageId);      // slow: done WITHOUT the lock
            lock.lock();
            frames[pageId].data = data;               // still here: loading pages are never evicted
            frames[pageId].loading = false;
            markUsed(pageId);
            pageLoaded.notify_all();                  // wake anyone waiting for this page (or for a free frame)
            return data;
        }
    }
};

// ---------- test ----------
const int NUM_PAGES = 40;
Disk* disk;
PageCache* cache;

void cpu(int seed) {
    mt19937 rng(seed);
    for (int i = 0; i < 300; i++) {
        int page = (rng() % 3 == 0) ? rng() % NUM_PAGES : rng() % 6;   // some hot pages, some random
        assert(cache->read(page) == "page-" + to_string(page));
    }
}

int main() {
    disk = new Disk(NUM_PAGES, 1);
    cache = new PageCache(8, *disk);
    vector<thread> cpus;
    for (int i = 0; i < 8; i++) cpus.push_back(thread(cpu, i + 1));
    for (int i = 0; i < 8; i++) cpus[i].join();
    assert(cache->hits + cache->misses == 8 * 300);
    assert(disk->reads == cache->misses);             // every miss read the disk exactly once
    cout << "Hits: " << cache->hits << ", misses (page faults): " << cache->misses << "\nSolution 1 OK\n";
    delete cache;
    delete disk;
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - writes: a "dirty" flag per page; dirty pages are written back to disk before eviction
//  - a frame STATE (Loading / Ready / WritingBack) instead of a single bool, because a page being
//    written back must also be waited for
//  - one helper, acquire(), returns a ready frame with the lock held; read() and write() both use it
//  - enum class for the three states

namespace advanced {

class WriteBackCache {
public:
    WriteBackCache(int capacity, Disk& disk) : capacity_(capacity), disk_(disk) {}

    std::string read(int pageId) {
        std::unique_lock lk(m_);
        return acquire(pageId, lk).data;
    }

    void write(int pageId, std::string data) {
        std::unique_lock lk(m_);
        Frame& f = acquire(pageId, lk);
        f.data = std::move(data);
        f.dirty = true;                                   // written back later, on eviction or flush
    }

    void flush() {                                        // call when no other thread is using the cache
        std::lock_guard lk(m_);
        for (auto& [id, f] : frames_)
            if (f.dirty) { disk_.writePage(id, f.data); f.dirty = false; }
    }

private:
    enum class State { Loading, Ready, WritingBack };
    struct Frame { std::string data; State state = State::Loading; bool dirty = false; };

    Frame& acquire(int pageId, std::unique_lock<std::mutex>& lk) {
        for (;;) {
            auto it = frames_.find(pageId);
            if (it != frames_.end()) {
                if (it->second.state == State::Ready) { touch(pageId); return it->second; }
                cv_.wait(lk);                             // loading or being written back: wait
                continue;
            }
            if ((int)frames_.size() >= capacity_) {
                if (lru_.empty()) { cv_.wait(lk); continue; }
                int victim = lru_.back();
                lru_.pop_back();
                pos_.erase(victim);
                Frame& v = frames_[victim];
                if (v.dirty) {
                    // Keep the entry (state WritingBack) while writing, so nobody re-reads the OLD
                    // contents from disk until the new ones are there.
                    v.state = State::WritingBack;
                    std::string copy = v.data;
                    lk.unlock();
                    disk_.writePage(victim, copy);
                    lk.lock();
                }
                frames_.erase(victim);
                cv_.notify_all();
                continue;                                 // re-check everything: time has passed
            }
            frames_[pageId].state = State::Loading;
            lk.unlock();
            std::string data = disk_.readPage(pageId);
            lk.lock();
            Frame& f = frames_[pageId];
            f.data = std::move(data);
            f.state = State::Ready;
            touch(pageId);
            cv_.notify_all();
            return f;
        }
    }

    void touch(int pageId) {
        if (auto p = pos_.find(pageId); p != pos_.end()) lru_.erase(p->second);
        lru_.push_front(pageId);
        pos_[pageId] = lru_.begin();
    }

    const int capacity_;
    Disk& disk_;
    std::mutex m_;
    std::condition_variable cv_;
    std::unordered_map<int, Frame> frames_;
    std::list<int> lru_;
    std::unordered_map<int, std::list<int>::iterator> pos_;
};

void run() {
    const int T = 6;
    Disk d(NUM_PAGES, 1);
    WriteBackCache c(6, d);
    std::vector<int> lastWritten(NUM_PAGES, -1);          // page p is only ever written by thread p % T
    std::vector<std::thread> ts;
    for (int t = 0; t < T; ++t) ts.emplace_back([&, t] {
        std::mt19937 rng(100 + t);
        int version = 0;
        for (int i = 0; i < 300; ++i) {
            int page = rng() % NUM_PAGES;
            if (page % T == t && rng() % 2) {
                c.write(page, "v" + std::to_string(++version) + "-" + std::to_string(page));
                lastWritten[page] = version;                // only this thread touches lastWritten[page]
            } else {
                std::string s = c.read(page);
                assert(s == "page-" + std::to_string(page) || s.back() == ('0' + page % 10));
            }
        }
    });
    for (auto& t : ts) t.join();
    c.flush();
    for (int p = 0; p < NUM_PAGES; ++p) {
        std::string expect = lastWritten[p] < 0 ? "page-" + std::to_string(p)
                           : "v" + std::to_string(lastWritten[p]) + "-" + std::to_string(p);
        assert(d.peek(p) == expect);                      // every write reached the disk, none lost
    }
    std::cout << "Disk reads " << d.reads << ", write-backs " << d.writes << "; every write persisted\nSolution 2 OK\n";
}
}  // namespace advanced

// Interview follow-ups to raise yourself:
//  - Eviction policy: LRU here. CLOCK (second-chance) approximates LRU with one bit per frame and no
//    list reordering on every hit, which is what real OS kernels use.
//  - Contention: one mutex for the whole cache. Shard by pageId (problem 21) to scale.
//  - Pinning: Solution 2 copies the data out. A real buffer pool hands out a pointer and keeps a pin
//    count; pinned pages can't be evicted until unpinned (RAII handle that unpins in its destructor).
//  - Prefetching: on a miss for page p, also start loading p+1 (sequential access is common).
//  - Thrashing: if the working set is bigger than memory, hit rate collapses; measure it (hits/misses).
//  - "Return the k most recent entries for a page" (a common extension): keep a small
//    ring buffer of versions per page inside the frame and return the newest k under the lock.
