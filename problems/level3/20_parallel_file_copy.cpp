// Multithreaded file copy with integrity check, resume and retries.
// Design:
//  - Split the file into fixed-size chunks; a pool of workers copies chunks with pread/pwrite
//    (positional I/O: no shared file offset, so no lock needed around reads and writes).
//  - Each chunk gets a checksum; after writing, re-read and verify. Failures retry up to N times.
//  - A manifest records which chunks are done, so a restarted copy skips them (resumable).
//  - Copying a chunk is idempotent: rewriting the same bytes at the same offset is harmless,
//    so retries and resumes are safe.
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <cassert>
#include <fstream>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - open / pread / pwrite / lseek / ftruncate (Linux system calls) — BASICS §12
//  - a checksum to verify each chunk after writing
//  - ifstream / ofstream in the test — BASICS §12

namespace advanced { void run(); }

// Simple version: split the file into chunks, workers grab chunk numbers one by one, copy
// the chunk, read it back, and check it matches. A failed check is retried.
// (Resuming an interrupted copy with a manifest is in Solution 2.)
const int CHUNK_SIZE = 64 * 1024;

// A simple checksum: any change in the bytes changes the number (with very high probability).
unsigned int checksum(char* bytes, int length) {
    unsigned int h = 2166136261u;
    for (int i = 0; i < length; i++) {
        h = h ^ (unsigned char)bytes[i];
        h = h * 16777619u;
    }
    return h;
}

class FileCopier {
    int source, target;                 // file descriptors
    long long fileSize;
    int totalChunks;
    int nextChunk;                      // next chunk number to hand out
    mutex m;

    // Hand out chunk numbers 0, 1, 2, ... ; -1 when there are none left.
    int takeChunk() {
        lock_guard<mutex> lock(m);
        if (nextChunk == totalChunks) return -1;
        return nextChunk++;
    }

    void worker() {
        vector<char> buffer(CHUNK_SIZE), check(CHUNK_SIZE);
        while (true) {
            int chunk = takeChunk();
            if (chunk == -1) return;
            long long offset = (long long)chunk * CHUNK_SIZE;
            int length = min((long long)CHUNK_SIZE, fileSize - offset);

            bool copied = false;
            for (int attempt = 0; attempt < 3 && !copied; attempt++) {
                // pread/pwrite read and write at an exact position, so threads don't share a
                // "current position" and need no lock around the I/O itself.
                // buffer.data() is a raw pointer to the vector's memory, which pread/pwrite need.
                pread(source, buffer.data(), length, offset);                 // read `length` bytes at `offset`
                unsigned int expected = checksum(buffer.data(), length);
                pwrite(target, buffer.data(), length, offset);                // write them at the same offset
                pread(target, check.data(), length, offset);           // read back what landed
                copied = checksum(check.data(), length) == expected;
                if (!copied) retries++;
            }
            if (!copied) failed = true;
            chunksCopied++;
        }
    }

public:
    atomic<int> chunksCopied{0}, retries{0};    // updated by all workers, so atomic (BASICS §6)
    atomic<bool> failed{false};

    bool copyFile(string from, string to, int numWorkers) {
        // open() returns a file descriptor: a small number standing for the open file (BASICS §12).
        // .c_str() turns a C++ string into the C-style string these system calls expect.
        source = open(from.c_str(), O_RDONLY);
        target = open(to.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (source < 0 || target < 0) return false;
        fileSize = lseek(source, 0, SEEK_END);                // jump to the end: the position = file size
        if (ftruncate(target, fileSize) != 0) return false;   // make the target full size up front
        totalChunks = (fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE;
        nextChunk = 0;

        vector<thread> workers;
        for (int i = 0; i < numWorkers; i++) workers.push_back(thread(&FileCopier::worker, this));
        for (int i = 0; i < numWorkers; i++) workers[i].join();

        close(source);
        close(target);
        return !failed;
    }
};

// ---------- test ----------
bool filesEqual(string a, string b) {
    ifstream x(a, ios::binary), y(b, ios::binary);
    string first((istreambuf_iterator<char>(x)), istreambuf_iterator<char>());
    string second((istreambuf_iterator<char>(y)), istreambuf_iterator<char>());
    return first == second;
}

int main() {
    string src = "/tmp/simple_copy_src.bin", dst = "/tmp/simple_copy_dst.bin";
    {
        ofstream out(src, ios::binary);
        for (int i = 0; i < 3 * 1024 * 1024 + 777; i++) out.put((char)(i * 31 + 7));
    }
    FileCopier copier;
    assert(copier.copyFile(src, dst, 4));
    assert(filesEqual(src, dst));
    cout << "Copied " << copier.chunksCopied << " chunks with 4 threads\nSolution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a manifest file so an interrupted copy can resume
//  - random corruption injected in the test to prove the retry path works — BASICS extras (mt19937)

namespace advanced {
constexpr size_t CHUNK = 64 * 1024;

uint32_t fnv1a(const char* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= (unsigned char)p[i]; h *= 16777619u; }
    return h;
}

class ParallelCopier {
    std::string src, dst, manifest;
    std::mutex manifestMu;
    std::vector<char> done;
    void loadManifest(size_t chunks) {
        done.assign(chunks, 0);
        std::ifstream in(manifest); size_t idx;
        while (in >> idx) if (idx < chunks) done[idx] = 1;
    }
    void markDone(size_t idx) {                    // append-only log; one writer at a time
        std::lock_guard lk(manifestMu);
        std::ofstream(manifest, std::ios::app) << idx << '\n';
    }
public:
    std::atomic<int> copied{0}, skipped{0}, retries{0};
    ParallelCopier(std::string s, std::string d) : src(std::move(s)), dst(std::move(d)), manifest(dst + ".manifest") {}

    bool copy(int workers, int maxRetries = 3, double injectFailureRate = 0.0) {
        int in = open(src.c_str(), O_RDONLY);
        int out = open(dst.c_str(), O_RDWR | O_CREAT, 0644);
        if (in < 0 || out < 0) return false;
        off_t size = lseek(in, 0, SEEK_END);
        if (ftruncate(out, size) != 0) return false;
        size_t chunks = (size + CHUNK - 1) / CHUNK;
        loadManifest(chunks);

        std::atomic<size_t> next{0}; std::atomic<bool> failed{false};
        auto worker = [&](unsigned seed) {
            std::mt19937 rng(seed); std::uniform_real_distribution<double> coin(0, 1);
            std::vector<char> buf(CHUNK), check(CHUNK);
            for (size_t i; (i = next++) < chunks && !failed;) {
                if (done[i]) { ++skipped; continue; }
                off_t off = (off_t)(i * CHUNK);
                size_t len = std::min<size_t>(CHUNK, size - off);
                bool ok = false;
                for (int attempt = 0; attempt <= maxRetries && !ok; ++attempt) {
                    if (attempt) ++retries;
                    if (pread(in, buf.data(), len, off) != (ssize_t)len) continue;
                    uint32_t want = fnv1a(buf.data(), len);
                    if (coin(rng) < injectFailureRate) buf[0] ^= 1;         // simulate corruption in transit
                    if (pwrite(out, buf.data(), len, off) != (ssize_t)len) continue;
                    if (pread(out, check.data(), len, off) != (ssize_t)len) continue;
                    ok = fnv1a(check.data(), len) == want;                  // verify what landed on disk
                }
                if (!ok) { failed = true; break; }
                markDone(i); ++copied;
            }
        };
        std::vector<std::thread> ts;
        for (int w = 0; w < workers; ++w) ts.emplace_back(worker, 1234 + w);
        for (auto& t : ts) t.join();
        fsync(out); close(in); close(out);
        if (!failed) std::remove(manifest.c_str());
        return !failed;
    }
};

bool sameContents(const std::string& a, const std::string& b) {
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(x), {}) == std::string(std::istreambuf_iterator<char>(y), {});
}

void run() {
    const std::string src = "/tmp/pcopy_src.bin", dst = "/tmp/pcopy_dst.bin";
    {
        std::ofstream f(src, std::ios::binary); std::mt19937 r(7);
        for (int i = 0; i < 5 * 1024 * 1024 + 123; ++i) f.put((char)r());
    }
    std::remove(dst.c_str()); std::remove((dst + ".manifest").c_str());

    { ParallelCopier c(src, dst); assert(c.copy(4, 5, 0.2)); assert(sameContents(src, dst));
      std::cout << "Copy with 20% injected corruption: copied " << c.copied << " chunks, " << c.retries << " retries\n"; }

    // Resume: pretend a previous run finished chunks 0..39, then run again.
    { std::ofstream m(dst + ".manifest"); for (int i = 0; i < 40; ++i) m << i << '\n'; }
    { ParallelCopier c(src, dst); assert(c.copy(4)); assert(sameContents(src, dst));
      std::cout << "Resumed run: skipped " << c.skipped << ", copied " << c.copied << "\nSolution 2 OK\n"; }
}
// Discussion points: chunk size vs. throughput; disk is often the bottleneck, so more threads
// stop helping (measure!); verify against a whole-file hash at the end; write to dst.tmp and
// rename() atomically so readers never see a half-copied file.
}  // namespace advanced
