// Manage tasks across worker nodes.
// "There are N worker nodes. Write APIs:
//    addTask(nodeId, taskId)  adds a task to a node's queue
//    runTask()                runs a task on a node
//    rebalanceTasks()         moves tasks from one node to another if some node has lots of
//                             CPU-intensive tasks
//    printTasks()             prints the queue at each node"
//
// KEY LEARNING POINTS
//  - Each node has its own queue. "Load" = the total cost of the tasks waiting on that node, so a
//    node with three heavy tasks counts as busier than one with five light ones.
//  - Rebalancing: repeatedly move a task from the most loaded node to the least loaded one, but only
//    while it actually helps. Moving a task of cost c narrows the gap only if c < (max - min);
//    otherwise you'd just swap which node is overloaded, forever.
//  - Take tasks off a queue under the lock, RUN them outside it.
//  - Solution 2: per-node locks so nodes don't block each other; rebalance locks two nodes, always in
//    the same order (lower id first) to avoid deadlock; and idle workers STEAL work from the busiest
//    node, which rebalances continuously without anyone calling rebalanceTasks().
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include <algorithm>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - vector<deque<Task>>: one queue per node — BASICS §10
//  - runTask returns the task to the caller, who runs it AFTER the lock is released — BASICS §8 rules
//  - stringstream to build the printTasks() output
//  - one mutex for everything: simplest correct answer

namespace advanced { void run(); }

struct Task {
    int id;
    int cost;                      // how CPU-heavy it is (1 = light, 10 = heavy)
};

class TaskManager {
    mutex m;
    vector<deque<Task>> queues;    // queues[node] = tasks waiting on that node
    vector<int> load;              // load[node] = total cost waiting on that node

    int busiest() { return max_element(load.begin(), load.end()) - load.begin(); }
    int idlest()  { return min_element(load.begin(), load.end()) - load.begin(); }

public:
    TaskManager(int numNodes) : queues(numNodes), load(numNodes, 0) {}

    void addTask(int node, int taskId, int cost) {
        lock_guard<mutex> lock(m);
        queues[node].push_back({taskId, cost});
        load[node] += cost;
    }

    // Takes the next task waiting on `node`. Returns false if there is none.
    // The caller runs the task after this returns, i.e. without holding the lock.
    bool runTask(int node, Task& out) {
        lock_guard<mutex> lock(m);
        if (queues[node].empty()) return false;
        out = queues[node].front();
        queues[node].pop_front();
        load[node] -= out.cost;
        return true;
    }

    // Returns how many tasks were moved.
    int rebalanceTasks() {
        lock_guard<mutex> lock(m);
        int moved = 0;
        while (true) {
            int from = busiest(), to = idlest();
            if (queues[from].empty()) break;
            Task t = queues[from].back();          // the newest task: it has waited the least
            if (t.cost >= load[from] - load[to]) break;   // moving it wouldn't narrow the gap
            queues[from].pop_back();
            load[from] -= t.cost;
            queues[to].push_back(t);
            load[to] += t.cost;
            moved++;
        }
        return moved;
    }

    string printTasks() {
        lock_guard<mutex> lock(m);
        stringstream out;
        for (int n = 0; n < (int)queues.size(); n++) {
            out << "node " << n << " (load " << load[n] << "):";
            for (Task& t : queues[n]) out << " " << t.id << "/" << t.cost;
            out << "\n";
        }
        return out.str();
    }

    int loadOf(int node) { lock_guard<mutex> lock(m); return load[node]; }
};

// ---------- test ----------
const int NUM_TASKS = 3000;
TaskManager* manager;
atomic<int> timesRun[NUM_TASKS];
atomic<bool> addingDone(false);       // tells the rebalancer to stop
atomic<bool> noMoreMoves(false);      // set only AFTER the rebalancer has stopped: workers may now finish

void worker(int node) {                     // one worker thread per node
    Task t;
    while (true) {
        if (manager->runTask(node, t)) {
            timesRun[t.id]++;               // "run" the task, outside the lock
        } else if (noMoreMoves) {
            // Safe to quit: no task can be added to or moved onto this node any more.
            // Quitting on "adding is done" alone would be a bug: the rebalancer could still move
            // tasks here after this worker left, and they would never run.
            if (!manager->runTask(node, t)) return;
            timesRun[t.id]++;
        } else {
            this_thread::yield();
        }
    }
}
void adder() {                              // everything lands on node 0: worst-case imbalance
    for (int i = 0; i < NUM_TASKS; i++) manager->addTask(0, i, 1 + i % 10);
}
void rebalancer() {
    while (!addingDone) { manager->rebalanceTasks(); this_thread::yield(); }
}

int main() {
    TaskManager small(3);
    small.addTask(0, 1, 9); small.addTask(0, 2, 1); small.addTask(0, 3, 1); small.addTask(0, 4, 1);
    small.addTask(1, 5, 2);
    // loads: node 0 = 12, node 1 = 2, node 2 = 0
    int moved = small.rebalanceTasks();
    assert(moved > 0);
    cout << small.printTasks();
    int hi = max({small.loadOf(0), small.loadOf(1), small.loadOf(2)});
    int lo = min({small.loadOf(0), small.loadOf(1), small.loadOf(2)});
    assert(hi - lo <= 9);                   // can't do better than the biggest single task

    manager = new TaskManager(4);
    thread add(adder), reb(rebalancer);
    vector<thread> workers;
    for (int n = 0; n < 4; n++) workers.push_back(thread(worker, n));
    add.join();
    addingDone = true;        // shutdown order matters: 1. stop adding
    reb.join();               //                         2. stop moving tasks
    noMoreMoves = true;       //                         3. only now let workers finish
    for (int n = 0; n < 4; n++) workers[n].join();
    for (int i = 0; i < NUM_TASKS; i++) assert(timesRun[i] == 1);   // every task ran exactly once
    delete manager;
    cout << "Solution 1 OK: every task ran exactly once\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - one mutex + condition variable PER NODE; worker threads sleep when their queue is empty
//  - rebalance locks two nodes, lower id first, so two rebalances can never deadlock
//  - work stealing: an idle worker takes the newest task from the busiest node (one lock at a time)
//  - load kept in an atomic per node, so picking the busiest node needs no lock (it's only a hint)
//  - clean shutdown: workers finish all queued work, then exit

namespace advanced {

class Cluster {
    struct Node {
        std::mutex m;
        std::condition_variable cv;
        std::deque<Task> q;
        std::atomic<int> load{0};
    };
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<std::thread> workers;
    std::function<void(int, const Task&)> execute;
    std::atomic<bool> stopping{false};

    int busiestNode() const {
        int best = 0;
        for (int i = 1; i < (int)nodes.size(); ++i) if (nodes[i]->load > nodes[best]->load) best = i;
        return best;
    }
    bool steal(int me, Task& out) {
        int victim = busiestNode();
        if (victim == me) return false;
        Node& v = *nodes[victim];
        std::lock_guard lk(v.m);                       // only one lock held: no deadlock possible
        if (v.q.empty()) return false;
        out = v.q.back(); v.q.pop_back();              // newest task: the owner works from the front
        v.load -= out.cost;
        return true;
    }
    void workerLoop(int me) {
        Node& n = *nodes[me];
        for (;;) {
            Task t; bool got = false;
            {
                std::unique_lock lk(n.m);
                n.cv.wait_for(lk, std::chrono::milliseconds(2), [&] { return !n.q.empty() || stopping; });
                if (!n.q.empty()) { t = n.q.front(); n.q.pop_front(); n.load -= t.cost; got = true; }
            }
            if (!got) got = steal(me, t);
            if (got) { execute(me, t); continue; }
            if (stopping && totalLoad() == 0) return;  // shut down only once ALL queues are empty
        }
    }
    int totalLoad() const { int s = 0; for (auto& n : nodes) s += n->load; return s; }

public:
    std::atomic<int> stolen{0};

    Cluster(int numNodes, std::function<void(int, const Task&)> exec) : execute(std::move(exec)) {
        for (int i = 0; i < numNodes; ++i) nodes.push_back(std::make_unique<Node>());
        for (int i = 0; i < numNodes; ++i) workers.emplace_back([this, i] { workerLoop(i); });
    }
    ~Cluster() {
        stopping = true;
        for (auto& n : nodes) { std::lock_guard lk(n->m); n->cv.notify_all(); }
        for (auto& w : workers) w.join();
    }

    void addTask(int node, Task t) {
        Node& n = *nodes[node];
        { std::lock_guard lk(n.m); n.q.push_back(t); n.load += t.cost; }
        n.cv.notify_one();
    }

    int rebalanceTasks() {
        int from = busiestNode(), to = 0;
        for (int i = 1; i < (int)nodes.size(); ++i) if (nodes[i]->load < nodes[to]->load) to = i;
        if (from == to) return 0;
        Node& a = *nodes[std::min(from, to)];
        Node& b = *nodes[std::max(from, to)];
        std::lock_guard la(a.m);                       // lower id first: a fixed global order
        std::lock_guard lb(b.m);
        Node& src = *nodes[from];
        Node& dst = *nodes[to];
        int moved = 0;
        while (!src.q.empty() && src.q.back().cost < src.load - dst.load) {
            Task t = src.q.back(); src.q.pop_back();
            src.load -= t.cost; dst.q.push_back(t); dst.load += t.cost;
            ++moved;
        }
        dst.cv.notify_one();
        return moved;
    }
};

void run() {
    std::vector<std::atomic<int>> ran(NUM_TASKS);
    std::vector<std::atomic<int>> perNode(4);
    {
        Cluster c(4, [&](int node, const Task& t) {
            ++ran[t.id]; ++perNode[node];
            std::this_thread::sleep_for(std::chrono::microseconds(20 * t.cost));
        });
        std::thread reb([&] { for (int i = 0; i < 50; ++i) { c.rebalanceTasks(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } });
        for (int i = 0; i < NUM_TASKS; ++i) c.addTask(0, {i, 1 + i % 10});   // all on node 0
        reb.join();
    }                                                  // destructor drains every queue, then stops
    for (int i = 0; i < NUM_TASKS; ++i) assert(ran[i] == 1);
    int helpers = 0;
    for (int n = 1; n < 4; ++n) helpers += perNode[n] > 0;
    assert(helpers > 0);                               // other nodes took work off node 0
    std::cout << "Solution 2 OK: tasks per node " << perNode[0] << " / " << perNode[1] << " / "
              << perNode[2] << " / " << perNode[3] << "\n";
}
}  // namespace advanced

// Interview follow-ups to raise yourself:
//  - What "load" means: queued cost (here), CPU usage reported by each node, or estimated finish time.
//    Ask which one the interviewer wants.
//  - Rebalance cost: scanning for max and min is O(N) per move; with many nodes keep loads in a
//    min-heap and a max-heap (or one ordered set) so each move is O(log N).
//  - Don't move a task that's already running; only queued ones (as here).
//  - Push vs pull: rebalanceTasks() pushes work on demand; work stealing pulls it continuously.
//    Stealing from the BACK while the owner takes from the FRONT keeps them from fighting over the
//    same end, which is how fork-join pools (Java, TBB) do it.
//  - Real cluster: nodes are machines, so queues live in a shared store and a "task" is assigned with
//    a lease that expires if the node dies.
