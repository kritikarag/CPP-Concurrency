// Job scheduler with dependencies.
// Jobs form a DAG; run every job after all its dependencies finish, as parallel as possible,
// on a fixed pool of workers. This is Kahn's topological sort where the "ready queue" is
// a blocking queue consumed by worker threads.

// KEY LEARNING POINTS
//  - Kahn's algorithm: each job counts its unfinished dependencies (`pending`). Jobs with 0 are ready.
//  - Finishing a job decrements pending for its dependents; any that hit 0 become ready.
//  - Parallel version: the ready set is a shared queue, workers pop and run jobs OUTSIDE the lock.
//  - Termination: stop when no job is ready AND none is running. If jobs remain at that point,
//    they can never become ready — there's a cycle.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include <queue>
#include <algorithm>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - vector<function<void()>> to store jobs — BASICS §7
//  - Kahn's topological sort, run by several worker threads

namespace advanced { void run(); }

// Jobs are numbered 0..n-1. dependsOn(a, b) means "a can only start after b finishes".
class JobScheduler {
    int n;
    vector<function<void()>> jobs;
    vector<vector<int>> unlocks;        // unlocks[b] = jobs waiting on b
    vector<int> remainingDeps;          // how many unfinished dependencies each job still has

    mutex m;
    condition_variable cv;
    queue<int> ready;                   // jobs whose dependencies are all done
    int finished;
    int running;

    void worker() {
        while (true) {
            int id;
            {
                unique_lock<mutex> lock(m);
                while (ready.empty() && running > 0) cv.wait(lock);  // maybe a running job will unlock more
                if (ready.empty()) {                                 // nothing ready, nothing running: stop
                    cv.notify_all();
                    return;
                }
                id = ready.front();
                ready.pop();
                running++;
            }

            jobs[id]();                                             // run the job without the lock

            {
                lock_guard<mutex> lock(m);
                running--;
                finished++;
                for (int next : unlocks[id]) {
                    remainingDeps[next]--;
                    if (remainingDeps[next] == 0) ready.push(next); // all its dependencies are done
                }
            }
            cv.notify_all();
        }
    }

public:
    int addJob(function<void()> job) {
        jobs.push_back(job);
        unlocks.push_back({});
        remainingDeps.push_back(0);
        return jobs.size() - 1;
    }

    void dependsOn(int job, int dependency) {
        unlocks[dependency].push_back(job);
        remainingDeps[job]++;
    }

    // Returns false if some jobs could never run (there's a cycle).
    bool runAll(int numWorkers) {
        n = jobs.size();
        finished = 0;
        running = 0;
        for (int i = 0; i < n; i++)
            if (remainingDeps[i] == 0) ready.push(i);       // jobs with no dependencies start right away

        vector<thread> workers;
        // thread(&JobScheduler::worker, this): run this object's worker() on a new thread (BASICS §8)
        for (int i = 0; i < numWorkers; i++) workers.push_back(thread(&JobScheduler::worker, this));
        for (int i = 0; i < numWorkers; i++) workers[i].join();
        return finished == n;
    }
};

// ---------- test ----------
mutex logMutex;
vector<string> runOrder;
void record(string name) {
    this_thread::sleep_for(chrono::milliseconds(20));
    lock_guard<mutex> lock(logMutex);
    runOrder.push_back(name);
}
void build()   { record("build"); }
void test()    { record("test"); }
void lint()    { record("lint"); }
void package() { record("package"); }
void deploy()  { record("deploy"); }
void docs()    { record("docs"); }
void doNothing() {}

int positionOf(string name) {
    for (int i = 0; i < (int)runOrder.size(); i++) if (runOrder[i] == name) return i;
    return -1;
}

int main() {
    JobScheduler s;
    int b = s.addJob(build), t = s.addJob(test), l = s.addJob(lint);
    int p = s.addJob(package), d = s.addJob(deploy);
    s.addJob(docs);                                      // no dependencies
    s.dependsOn(t, b); s.dependsOn(l, b);
    s.dependsOn(p, t); s.dependsOn(p, l);
    s.dependsOn(d, p);
    assert(s.runAll(4));
    assert(positionOf("build") < positionOf("test") && positionOf("build") < positionOf("lint"));
    assert(positionOf("package") < positionOf("deploy"));
    for (string name : runOrder) cout << name << " ";
    cout << "\n";

    JobScheduler cyclic;
    int x = cyclic.addJob(doNothing), y = cyclic.addJob(doNothing), w = cyclic.addJob(doNothing);
    cyclic.dependsOn(y, x); cyclic.dependsOn(w, y); cyclic.dependsOn(y, w);   // y and w wait on each other
    assert(!cyclic.runAll(2));
    cout << "Cycle detected\nSolution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - compare_exchange_weak to track the peak number of jobs running at once — BASICS §18

namespace advanced {
class DagScheduler {
    struct Job { std::function<void()> fn; std::vector<int> dependents; int pending = 0; };
    std::vector<Job> jobs;
public:
    int add(std::function<void()> fn) { jobs.push_back({std::move(fn)}); return (int)jobs.size() - 1; }
    void depends(int job, int on) { jobs[on].dependents.push_back(job); ++jobs[job].pending; }

    bool run(int workers) {                          // false if there is a cycle
        std::mutex m; std::condition_variable cv;
        std::queue<int> ready; int remaining = (int)jobs.size();
        for (int i = 0; i < (int)jobs.size(); ++i) if (jobs[i].pending == 0) ready.push(i);
        if (ready.empty() && remaining) return false;
        int running = 0; bool stuck = false;

        auto worker = [&] {
            for (;;) {
                int id;
                {
                    std::unique_lock lk(m);
                    cv.wait(lk, [&] { return !ready.empty() || remaining == 0 || (running == 0 && ready.empty()); });
                    if (ready.empty()) {               // either all done, or a cycle left jobs unreachable
                        if (remaining) stuck = true;
                        cv.notify_all();
                        return;
                    }
                    id = ready.front(); ready.pop(); ++running;
                }
                jobs[id].fn();
                {
                    std::lock_guard lk(m);
                    --running; --remaining;
                    for (int d : jobs[id].dependents) if (--jobs[d].pending == 0) ready.push(d);   // unlock dependents
                }
                cv.notify_all();
            }
        };
        std::vector<std::thread> ts;
        for (int i = 0; i < workers; ++i) ts.emplace_back(worker);
        for (auto& t : ts) t.join();
        return !stuck;
    }
};

void run() {
    // build -> {test, lint} -> package -> deploy ; docs independent
    DagScheduler s; std::mutex om; std::vector<std::string> log;
    std::atomic<int> concurrent{0}, peak{0};
    auto job = [&](std::string name) {
        return [&, name] {
            int c = ++concurrent; int p = peak; while (c > p && !peak.compare_exchange_weak(p, c)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            { std::lock_guard lk(om); log.push_back(name); }
            --concurrent;
        };
    };
    int build = s.add(job("build")), test = s.add(job("test")), lint = s.add(job("lint")),
        pkg = s.add(job("package")), deploy = s.add(job("deploy")), docs = s.add(job("docs"));
    s.depends(test, build); s.depends(lint, build);
    s.depends(pkg, test); s.depends(pkg, lint); s.depends(deploy, pkg);
    (void)docs;
    assert(s.run(4));
    auto pos = [&](const char* n) { return std::find(log.begin(), log.end(), n) - log.begin(); };
    assert(pos("build") < pos("test") && pos("build") < pos("lint"));
    assert(pos("test") < pos("package") && pos("lint") < pos("package") && pos("package") < pos("deploy"));
    for (auto& n : log) std::cout << n << ' ';
    std::cout << "\nPeak parallelism: " << peak << "\n";

    DagScheduler cyc; int a = cyc.add([] {}), b = cyc.add([] {}), c = cyc.add([] {});
    cyc.depends(b, a); cyc.depends(c, b); cyc.depends(b, c);         // a -> b <-> c
    assert(!cyc.run(2));
    std::cout << "Cycle detected\nSolution 2 OK\n";
}
}  // namespace advanced
