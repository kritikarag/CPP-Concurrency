// Reader-Writer lock built from scratch, three ways, then std::shared_mutex.
// Many readers may hold it together; a writer needs it alone.
//  1. ReaderPreferring — simplest; writers can STARVE under a steady stream of readers.
//  2. WriterPreferring — new readers wait while any writer is waiting; readers can starve instead.
//  3. std::shared_mutex — what you'd use in real code (policy is implementation-defined).
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <shared_mutex>
#include <atomic>
#include <vector>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a reader count + a writing flag; `waitingWriters` to stop writer starvation
//  - atomic counters in the test to prove no reader overlaps a writer — BASICS §6

namespace advanced { void run(); }

// 1a. Reader-preferring: simplest. Readers never wait for WAITING writers, so writers can starve.
class ReadWriteLock {
    mutex m;
    condition_variable cv;
    int readers;                    // how many readers are inside right now
    bool writing;                   // is a writer inside right now?

public:
    ReadWriteLock() {
        readers = 0;
        writing = false;
    }

    void readLock() {
        unique_lock<mutex> lock(m);
        while (writing) cv.wait(lock);              // readers only wait for an ACTIVE writer
        readers++;
    }

    void readUnlock() {
        unique_lock<mutex> lock(m);
        readers--;
        if (readers == 0) cv.notify_all();          // last reader out lets a writer in
    }

    void writeLock() {
        unique_lock<mutex> lock(m);
        while (writing || readers > 0) cv.wait(lock);   // writer needs the room empty
        writing = true;
    }

    void writeUnlock() {
        unique_lock<mutex> lock(m);
        writing = false;
        cv.notify_all();
    }
};

// 1b. Writer-preferring: as soon as a writer is WAITING, new readers wait too. No writer starvation.
class WriterFirstLock {
    mutex m;
    condition_variable cv;
    int readers;
    int waitingWriters;             // writers queued up -> new readers must hold back
    bool writing;

public:
    WriterFirstLock() {
        readers = 0;
        waitingWriters = 0;
        writing = false;
    }

    void readLock() {
        unique_lock<mutex> lock(m);
        while (writing || waitingWriters > 0) cv.wait(lock);
        readers++;
    }

    void readUnlock() {
        unique_lock<mutex> lock(m);
        readers--;
        if (readers == 0) cv.notify_all();
    }

    void writeLock() {
        unique_lock<mutex> lock(m);
        waitingWriters++;
        while (writing || readers > 0) cv.wait(lock);
        waitingWriters--;
        writing = true;
    }

    void writeUnlock() {
        unique_lock<mutex> lock(m);
        writing = false;
        cv.notify_all();
    }
};

// ---------- test ----------
ReadWriteLock rw1;
WriterFirstLock rw2;
// Test-only counters: atomic so every thread can update them safely without a lock (BASICS §6).
atomic<int> activeReaders(0), activeWriters(0);
int sharedValue = 0;

void reader1() {
    for (int i = 0; i < 3000; i++) {
        rw1.readLock();
        activeReaders++;
        assert(activeWriters == 0);                 // no writer while a reader is inside
        activeReaders--;
        rw1.readUnlock();
    }
}
void writer1() {
    for (int i = 0; i < 3000; i++) {
        rw1.writeLock();
        activeWriters++;
        assert(activeWriters == 1 && activeReaders == 0);   // writer is alone
        sharedValue++;
        activeWriters--;
        rw1.writeUnlock();
    }
}
void reader2() {
    for (int i = 0; i < 3000; i++) {
        rw2.readLock(); activeReaders++; assert(activeWriters == 0); activeReaders--; rw2.readUnlock();
    }
}
void writer2() {
    for (int i = 0; i < 3000; i++) {
        rw2.writeLock(); activeWriters++; assert(activeWriters == 1 && activeReaders == 0);
        sharedValue++; activeWriters--; rw2.writeUnlock();
    }
}

int main() {
    vector<thread> threads;
    for (int i = 0; i < 4; i++) threads.push_back(thread(reader1));
    for (int i = 0; i < 2; i++) threads.push_back(thread(writer1));
    for (int i = 0; i < (int)threads.size(); i++) threads[i].join();
    threads.clear();
    for (int i = 0; i < 4; i++) threads.push_back(thread(reader2));
    for (int i = 0; i < 2; i++) threads.push_back(thread(writer2));
    for (int i = 0; i < (int)threads.size(); i++) threads[i].join();
    assert(sharedValue == 12000);
    cout << "Solution 1 OK: reader-preferring and writer-preferring\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - shared_mutex, shared_lock — BASICS §16
//  - templates: one stress<RW>() test that works for all three lock types — BASICS §14

namespace advanced {
class ReaderPreferringRW {
    std::mutex m; std::condition_variable cv;
    int readers = 0; bool writer = false;
public:
    void lock_shared()   { std::unique_lock lk(m); cv.wait(lk, [&] { return !writer; }); ++readers; }
    void unlock_shared() { std::lock_guard lk(m); if (--readers == 0) cv.notify_all(); }
    void lock()          { std::unique_lock lk(m); cv.wait(lk, [&] { return !writer && readers == 0; }); writer = true; }
    void unlock()        { std::lock_guard lk(m); writer = false; cv.notify_all(); }
};

class WriterPreferringRW {
    std::mutex m; std::condition_variable readersCv, writersCv;
    int readers = 0, waitingWriters = 0; bool writer = false;
public:
    void lock_shared() {
        std::unique_lock lk(m);
        readersCv.wait(lk, [&] { return !writer && waitingWriters == 0; });   // yield to waiting writers
        ++readers;
    }
    void unlock_shared() {
        std::lock_guard lk(m);
        if (--readers == 0) writersCv.notify_one();
    }
    void lock() {
        std::unique_lock lk(m);
        ++waitingWriters;
        writersCv.wait(lk, [&] { return !writer && readers == 0; });
        --waitingWriters; writer = true;
    }
    void unlock() {
        std::lock_guard lk(m);
        writer = false;
        if (waitingWriters > 0) writersCv.notify_one(); else readersCv.notify_all();
    }
};

// Test harness: checks the invariant "no writer overlaps anyone" on every access.
template <typename RW>
void stress(const char* name) {
    RW rw; std::atomic<int> activeReaders{0}, activeWriters{0}; long data = 0;
    std::vector<std::thread> ts;
    for (int r = 0; r < 6; ++r) ts.emplace_back([&] {
        for (int i = 0; i < 5000; ++i) {
            std::shared_lock lk(rw);           // works with any type providing lock_shared/unlock_shared
            ++activeReaders; assert(activeWriters == 0); volatile long x = data; (void)x; --activeReaders;
        }
    });
    for (int w = 0; w < 2; ++w) ts.emplace_back([&] {
        for (int i = 0; i < 5000; ++i) {
            std::unique_lock lk(rw);
            assert(++activeWriters == 1 && activeReaders == 0); ++data; --activeWriters;
        }
    });
    for (auto& t : ts) t.join();
    assert(data == 10000);
    std::cout << "Solution 2 OK: " << name << "\n";
}

void run() {
    stress<ReaderPreferringRW>("reader-preferring");
    stress<WriterPreferringRW>("writer-preferring");
    stress<std::shared_mutex>("std::shared_mutex");
}
// Follow-ups: a FAIR lock (ticket/turnstile — Little Book of Semaphores 4.2.6), lock upgrade
// (shared -> exclusive) and why naive upgrade deadlocks when two readers try it at once.
}  // namespace advanced
