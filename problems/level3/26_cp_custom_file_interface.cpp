// Implement `cp` on top of a custom file interface (a common variant: "optimise cp for a
// floppy-to-SSD transfer").
//
// You're given an interface like:
//     int read(char* buf, int n);          // returns bytes read (MAY BE FEWER than n), 0 at EOF, -1 on error
//     int write(const char* buf, int n);   // returns bytes written (MAY BE FEWER than n), -1 on error
// Copy the source file to the destination, correctly and fast.
//
// KEY LEARNING POINTS
//  - Short reads and short writes are normal. The #1 bug is assuming write() wrote everything:
//    always loop until all bytes are written (writeAll below).
//  - 0 from read() means end of file; -1 means error. Handle both.
//  - A plain loop alternates "wait for the source, then wait for the destination". If both are slow
//    (a floppy and a disk), a reader thread and a writer thread joined by a BOUNDED queue overlap
//    the two waits: total time drops from (read time + write time) to roughly the slower of the two.
//  - Bounded, so a fast reader can't fill memory with a 4 GB file while the writer lags (backpressure).
//  - Errors must stop BOTH threads: if the writer fails, the reader may be blocked on a full queue,
//    and if the reader fails, the writer may be blocked on an empty one. A shared "failed" flag that
//    wakes everyone is the fix. Forgetting this is the classic shutdown hang.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <queue>
#include <deque>
#include <random>
#include <cstring>
#include <algorithm>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - an abstract class (`virtual ... = 0`): the interface you're given; the test supplies a fake one
//  - memcpy(dest, src, n): copy n raw bytes (from <cstring>)
//  - a bounded queue whose push/pop return false once the copy has failed, so threads can stop — BASICS §5
//  - thread(readerLoop, &src, &queue, bufSize): passing pointers to a thread function — BASICS §8

namespace advanced { void run(); }

// The interface from the interview.
class File {
public:
    virtual ~File() {}
    virtual int read(char* buf, int n) = 0;
    virtual int write(const char* buf, int n) = 0;
};

// A fake file for testing: in memory, slow, returns short reads/writes, and can be told to fail.
// Each SlowFile is used by only one thread at a time, so it needs no lock of its own.
class SlowFile : public File {
    string data;
    size_t readPos = 0;
    int maxPerCall, latencyMs, failAfter;      // failAfter = number of successful calls before -1 (-1 = never)
    int calls = 0;
public:
    SlowFile(string initial, int maxPerCall, int latencyMs, int failAfter = -1)
        : data(initial), maxPerCall(maxPerCall), latencyMs(latencyMs), failAfter(failAfter) {}

    int read(char* buf, int n) override {
        this_thread::sleep_for(chrono::milliseconds(latencyMs));
        if (failAfter >= 0 && calls >= failAfter) return -1;
        calls++;
        if (readPos >= data.size()) return 0;
        int k = min({n, maxPerCall, (int)(data.size() - readPos)});
        memcpy(buf, data.data() + readPos, k);
        readPos += k;
        return k;
    }
    int write(const char* buf, int n) override {
        this_thread::sleep_for(chrono::milliseconds(latencyMs));
        if (failAfter >= 0 && calls >= failAfter) return -1;
        calls++;
        int k = min(n, maxPerCall);                // deliberately short writes
        data.append(buf, k);
        return k;
    }
    string contents() { return data; }
};

// Keep calling write() until all n bytes are written. Returns false on error.
bool writeAll(File& f, const char* buf, int n) {
    int done = 0;
    while (done < n) {
        int written = f.write(buf + done, n - done);
        if (written <= 0) return false;
        done += written;
    }
    return true;
}

// Step 1: the simple version. One thread: read a chunk, write it, repeat.
bool copySimple(File& src, File& dst, int bufSize) {
    vector<char> buf(bufSize);
    while (true) {
        int n = src.read(buf.data(), bufSize);
        if (n < 0) return false;                   // read error
        if (n == 0) return true;                   // end of file: done
        if (!writeAll(dst, buf.data(), n)) return false;
    }
}

// Step 2: a bounded queue of chunks between a reader thread and a writer thread.
class ChunkQueue {
    queue<string> chunks;
    int capacity;
    bool finished = false;                         // reader hit end of file
    bool failed = false;                           // someone hit an error: everybody stop
    mutex m;
    condition_variable notFull, notEmpty;

public:
    ChunkQueue(int capacity) { this->capacity = capacity; }

    bool push(string chunk) {                      // false = the copy has failed, stop reading
        unique_lock<mutex> lock(m);
        while ((int)chunks.size() == capacity && !failed) notFull.wait(lock);
        if (failed) return false;
        chunks.push(chunk);
        notEmpty.notify_one();
        return true;
    }

    bool pop(string& chunk) {                      // false = nothing more to write (done, or failed)
        unique_lock<mutex> lock(m);
        while (chunks.empty() && !finished && !failed) notEmpty.wait(lock);
        if (failed || chunks.empty()) return false;
        chunk = chunks.front();
        chunks.pop();
        notFull.notify_one();
        return true;
    }

    void finish() {
        lock_guard<mutex> lock(m);
        finished = true;
        notEmpty.notify_all();
    }

    void fail() {                                  // wake BOTH sides so neither sleeps forever
        lock_guard<mutex> lock(m);
        failed = true;
        notFull.notify_all();
        notEmpty.notify_all();
    }

    bool hasFailed() {
        lock_guard<mutex> lock(m);
        return failed;
    }
};

void readerLoop(File* src, ChunkQueue* q, int bufSize) {
    vector<char> buf(bufSize);
    while (true) {
        int n = src->read(buf.data(), bufSize);
        if (n < 0) { q->fail(); return; }
        if (n == 0) { q->finish(); return; }
        if (!q->push(string(buf.data(), n))) return;      // writer failed: stop
    }
}

bool copyPipelined(File& src, File& dst, int bufSize, int queueCapacity) {
    ChunkQueue q(queueCapacity);
    thread reader(readerLoop, &src, &q, bufSize);
    string chunk;
    while (q.pop(chunk)) {                                 // this thread is the writer
        if (!writeAll(dst, chunk.data(), chunk.size())) { q.fail(); break; }
    }
    reader.join();                                         // always join, success or failure
    return !q.hasFailed();
}

// ---------- test ----------
string makeData(int size) {
    mt19937 rng(7);
    string s(size, ' ');
    for (int i = 0; i < size; i++) s[i] = (char)(rng() & 0xff);
    return s;
}

long long millisSince(chrono::steady_clock::time_point start) {
    return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();
}

int main() {
    string data = makeData(200 * 1024);

    SlowFile src1(data, 4096, 2), dst1("", 3000, 2);
    auto t0 = chrono::steady_clock::now();
    assert(copySimple(src1, dst1, 8192));
    long long simpleMs = millisSince(t0);
    assert(dst1.contents() == data);

    SlowFile src2(data, 4096, 2), dst2("", 3000, 2);
    t0 = chrono::steady_clock::now();
    assert(copyPipelined(src2, dst2, 8192, 4));
    long long pipeMs = millisSince(t0);
    assert(dst2.contents() == data);
    cout << "Simple copy: " << simpleMs << " ms, pipelined: " << pipeMs << " ms\n";
    assert(pipeMs < simpleMs);

    // Errors on either side must make copyPipelined return false WITHOUT hanging.
    SlowFile src3(data, 4096, 1), badDst("", 3000, 1, 10);
    assert(!copyPipelined(src3, badDst, 8192, 2));
    SlowFile badSrc(data, 4096, 1, 5), dst4("", 3000, 1);
    assert(!copyPipelined(badSrc, dst4, 8192, 2));
    cout << "Read and write errors stop both threads\nSolution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a fixed pool of reusable buffers instead of allocating a new string per chunk ("double/triple
//    buffering"): two lists of buffer numbers, free and full, passed between the two threads
//  - the buffer CONTENTS are touched without the lock: handing the buffer number over through the
//    mutex is what makes it safe (the mutex creates the happens-before edge) — GLOSSARY
//  - one condition variable for both threads with notify_all: fine with only two threads

namespace advanced {

bool copyWithBufferPool(File& src, File& dst, int numBuffers, int bufSize) {
    std::vector<std::vector<char>> buffers(numBuffers, std::vector<char>(bufSize));
    std::vector<int> length(numBuffers, 0);
    std::deque<int> freeBufs, fullBufs;
    for (int i = 0; i < numBuffers; ++i) freeBufs.push_back(i);
    std::mutex m;
    std::condition_variable cv;
    bool eof = false, failed = false;

    std::thread reader([&] {
        for (;;) {
            int i;
            {
                std::unique_lock lk(m);
                cv.wait(lk, [&] { return !freeBufs.empty() || failed; });
                if (failed) return;
                i = freeBufs.front(); freeBufs.pop_front();
            }
            int n = src.read(buffers[i].data(), bufSize);      // I own buffer i now: no lock needed
            {
                std::lock_guard lk(m);
                if (n < 0) failed = true;
                else if (n == 0) { eof = true; freeBufs.push_back(i); }
                else { length[i] = n; fullBufs.push_back(i); }
            }
            cv.notify_all();
            if (n <= 0) return;
        }
    });

    for (;;) {                                                 // writer
        int i;
        {
            std::unique_lock lk(m);
            cv.wait(lk, [&] { return !fullBufs.empty() || eof || failed; });
            if (failed || fullBufs.empty()) break;             // error, or end of file and drained
            i = fullBufs.front(); fullBufs.pop_front();
        }
        bool ok = writeAll(dst, buffers[i].data(), length[i]);
        {
            std::lock_guard lk(m);
            if (ok) freeBufs.push_back(i); else failed = true;
        }
        cv.notify_all();
        if (!ok) break;
    }
    reader.join();
    std::lock_guard lk(m);
    return !failed;
}

void run() {
    std::string data = makeData(200 * 1024);
    SlowFile src(data, 4096, 2), dst("", 3000, 2);
    auto t0 = std::chrono::steady_clock::now();
    assert(copyWithBufferPool(src, dst, 3, 8192));
    std::cout << "Buffer-pool copy: " << millisSince(t0) << " ms\n";
    assert(dst.contents() == data);

    SlowFile s2(data, 4096, 1), badDst("", 3000, 1, 10);
    assert(!copyWithBufferPool(s2, badDst, 3, 8192));
    SlowFile badSrc(data, 4096, 1, 5), d3("", 3000, 1);
    assert(!copyWithBufferPool(badSrc, d3, 3, 8192));
    SlowFile empty("", 4096, 1), d4("", 3000, 1);
    assert(copyWithBufferPool(empty, d4, 3, 8192) && d4.contents().empty());
    std::cout << "Solution 2 OK\n";
}
}  // namespace advanced

// Interview follow-ups to raise yourself:
//  - Buffer size: bigger chunks mean fewer calls (less per-call overhead) but more memory per buffer.
//    Match the device's block size or a multiple of it (e.g. 64 KB-1 MB).
//  - Floppy-to-SSD specifically: the floppy is the bottleneck, so the goal is to keep it reading
//    continuously; 2-3 buffers are enough because the SSD drains them far faster than they fill.
//  - Several writer threads only help if the destination supports positional writes (pwrite at an
//    offset), as in problem 20. With a plain sequential write(), one writer is the only correct choice.
//  - Durability: call fsync at the end and write to dst.tmp then rename(), so a crash never leaves a
//    half-copied file under the real name.
//  - Partial failure: delete the partial destination, or keep a manifest to resume (problem 20).
