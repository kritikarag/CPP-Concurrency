// Search-Insert-Delete on a shared linked list.
// Rules:  searchers may run with each other and with one inserter;
//         inserters are mutually exclusive with each other;
//         a deleter excludes everyone.
// A three-way generalisation of readers-writers (Little Book of Semaphores 6.1).
//
// Why searchers can safely run alongside an inserter: the list only grows at the tail, and the
// new node is published with ONE atomic store (release). A searcher traversing with acquire
// loads sees either the old end (nullptr) or a fully built node — never half of one.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <vector>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - three kinds of thread with different rules, all checked in while loops
//  - atomic<int> length with load()/store(): write the item first, then publish the new length — BASICS §6

namespace advanced { void run(); }

// Beginner version: the "rules of entry" are the interesting part; the list is a fixed array.
// Inserters only ever ADD at the end and publish the new length with an atomic store, so a searcher
// running at the same time sees either the old length or the new one, never a half-written item.
class SearchInsertDelete {
    mutex m;
    condition_variable cv;
    int searchers;                  // searchers inside
    bool inserting;                 // an inserter inside?
    bool deleting;                  // a deleter inside?
    int waitingDeleters;            // so deleters aren't starved by endless searchers

    int items[100000];
    atomic<int> length;             // atomic: searchers read it while the inserter writes it (BASICS §6)

public:
    SearchInsertDelete() {
        searchers = 0; inserting = false; deleting = false; waitingDeleters = 0;
        length = 0;
    }

    bool search(int x) {
        {
            unique_lock<mutex> lock(m);
            while (deleting || waitingDeleters > 0) cv.wait(lock);    // searchers only fear deleters
            searchers++;
        }
        bool found = false;
        int n = length.load();                                          // snapshot of the length
        for (int i = 0; i < n; i++) if (items[i] == x) { found = true; break; }
        {
            unique_lock<mutex> lock(m);
            searchers--;
            if (searchers == 0) cv.notify_all();
        }
        return found;
    }

    void insert(int x) {
        {
            unique_lock<mutex> lock(m);
            while (deleting || inserting || waitingDeleters > 0) cv.wait(lock);   // one inserter at a time
            inserting = true;
        }
        int n = length.load();
        items[n] = x;                   // write the item first...
        length.store(n + 1);            // ...then make it visible
        {
            unique_lock<mutex> lock(m);
            inserting = false;
            cv.notify_all();
        }
    }

    void remove(int x) {
        {
            unique_lock<mutex> lock(m);
            waitingDeleters++;
            while (deleting || inserting || searchers > 0) cv.wait(lock);   // deleter needs everyone out
            waitingDeleters--;
            deleting = true;
        }
        int n = length.load(), kept = 0;                // alone now: compact the array
        for (int i = 0; i < n; i++) if (items[i] != x) items[kept++] = items[i];
        length.store(kept);
        {
            unique_lock<mutex> lock(m);
            deleting = false;
            cv.notify_all();
        }
    }
};

// ---------- test ----------
SearchInsertDelete* list1;
void runSearcher() { for (int i = 0; i < 2000; i++) list1->search(i % 50); }
void runInserter() { for (int i = 0; i < 1000; i++) list1->insert(i % 50); }
void runDeleter()  { for (int i = 0; i < 300; i++)  list1->remove(i % 50); }

int main() {
    list1 = new SearchInsertDelete();
    vector<thread> threads;
    for (int i = 0; i < 4; i++) threads.push_back(thread(runSearcher));
    for (int i = 0; i < 2; i++) threads.push_back(thread(runInserter));
    threads.push_back(thread(runDeleter));
    for (int i = 0; i < (int)threads.size(); i++) threads[i].join();
    delete list1;
    cout << "Solution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a hand-built linked list whose `next` pointers are atomic — BASICS §6
//  - memory_order_release / acquire for safe publishing — BASICS §18

namespace advanced {
class SIDList {
    struct Node { int value; std::atomic<Node*> next{nullptr}; explicit Node(int v) : value(v) {} };
    Node head{0};                                   // sentinel
    Node* tail = &head;                             // touched only by the (single) inserter or deleter

    std::mutex m; std::condition_variable cv;
    int searchers = 0, waitingDeleters = 0; bool inserter = false, deleter = false;
public:
    std::atomic<int> s{0}, i{0}, d{0};              // instrumentation for the test

    ~SIDList() { for (Node* n = head.next.load(); n;) { Node* nx = n->next.load(); delete n; n = nx; } }

    bool search(int x) {
        { std::unique_lock lk(m); cv.wait(lk, [&] { return !deleter && waitingDeleters == 0; }); ++searchers; }
        ++s; assert(d == 0);
        bool found = false;
        for (Node* n = head.next.load(std::memory_order_acquire); n; n = n->next.load(std::memory_order_acquire))
            if (n->value == x) { found = true; break; }
        --s;
        { std::lock_guard lk(m); if (--searchers == 0) cv.notify_all(); }
        return found;
    }
    void insert(int x) {
        { std::unique_lock lk(m); cv.wait(lk, [&] { return !deleter && !inserter && waitingDeleters == 0; }); inserter = true; }
        ++i; assert(i == 1 && d == 0);
        Node* n = new Node(x);                      // fully built before publishing
        tail->next.store(n, std::memory_order_release);
        tail = n;
        --i;
        { std::lock_guard lk(m); inserter = false; cv.notify_all(); }
    }
    void remove(int x) {
        {
            std::unique_lock lk(m);
            ++waitingDeleters;                      // block NEW searchers/inserters so deleters can't starve
            cv.wait(lk, [&] { return !deleter && !inserter && searchers == 0; });
            --waitingDeleters; deleter = true;
        }
        ++d; assert(d == 1 && s == 0 && i == 0);
        Node* prev = &head;                         // exclusive: plain relinking is safe
        for (Node* n = prev->next.load(); n; n = prev->next.load()) {
            if (n->value == x) { prev->next.store(n->next.load()); if (tail == n) tail = prev; delete n; }
            else prev = n;
        }
        --d;
        { std::lock_guard lk(m); deleter = false; cv.notify_all(); }
    }
};

void run() {
    SIDList l; std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) ts.emplace_back([&] { for (int k = 0; k < 3000; ++k) l.search(k % 50); });
    for (int t = 0; t < 2; ++t) ts.emplace_back([&] { for (int k = 0; k < 1000; ++k) l.insert(k % 50); });
    ts.emplace_back([&] { for (int k = 0; k < 500; ++k) l.remove(k % 50); });
    for (auto& t : ts) t.join();
    std::cout << "Solution 2 OK: deleters always exclusive, at most one inserter, searchers race-free\n";
}
// Discuss: waitingDeleters gives deleters priority (else a stream of searchers starves them),
// at the cost of possibly starving searchers under constant deletes — the usual RW trade-off.
}  // namespace advanced
