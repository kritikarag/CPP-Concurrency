# Concurrency theory notes

> Unfamiliar word? See the [glossary](GLOSSARY.md).

Original explanations written for the first 15 question topics of
[Devinterview-io/concurrency-interview-questions](https://github.com/Devinterview-io/concurrency-interview-questions),
with interview angles and links to the C++ examples in this repo.

---

## 1. Concurrency vs parallelism
**Concurrency** is about *structure*: a program is organised so several tasks can be in progress at once, with their steps interleaved. **Parallelism** is about *execution*: tasks literally run at the same instant on different cores.

A single-core machine can be concurrent (the OS time-slices threads) but never parallel. A GPU running one kernel over a million pixels is parallel with very little concurrency logic. Most server code is both.

**Interview angle:** concurrency is mainly about correctness (coordination, shared state); parallelism is mainly about speed. Amdahl's law caps parallel speed-up by the serial fraction of the work.
**Code:** [`01_concurrency_vs_parallelism.cpp`](concepts/01_concurrency_vs_parallelism.cpp)

## 2. Race conditions
A race condition exists when the result depends on the timing of threads. The classic case is `counter++`, which is really *load → add → store*. Two threads can both load 5, both store 6, and one increment is lost.

A **data race** (C++ term) is narrower: two threads access the same memory without synchronisation and at least one writes. In C++ a data race is **undefined behaviour**, not just a wrong number. Races can exist without data races too (check-then-act on a thread-safe map: `if (!m.contains(k)) m.insert(k, v)`).

**Fixes:** a mutex around the critical section, atomics for single-variable updates, avoiding shared mutable state (immutability, message passing, thread confinement).
**Code:** [`02_race_condition.cpp`](concepts/02_race_condition.cpp) — build with `-fsanitize=thread` to have TSan detect it.

## 3. Critical sections
A critical section is the code that touches shared state and must not be executed by two threads at once. A correct solution needs:

- **Mutual exclusion** — at most one thread inside.
- **Progress** — if nobody is inside, someone waiting can enter.
- **Bounded waiting** — no thread waits forever (no starvation).

Keep critical sections **short**: no I/O, sleeping, or calls into unknown code while holding a lock. Long critical sections serialise your program and invite deadlock.

## 4. How mutual exclusion is implemented
Bottom up:

1. **Hardware atomics** — instructions like compare-and-swap (CAS), test-and-set, fetch-and-add, which the CPU executes indivisibly using cache-coherence protocols.
2. **Spinlocks** — loop on an atomic until you acquire it. Cheap for very short waits, wasteful otherwise.
3. **Kernel-assisted locks** — on Linux, a `std::mutex` is a futex: uncontended lock/unlock is a single atomic in user space; only under contention does the thread sleep in the kernel.
4. Older/uniprocessor kernels also disabled interrupts around critical sections.

**Interview angle:** know why a pure-software algorithm like Peterson's needs memory fences on modern CPUs (reordering).
**Code:** [`04_mutual_exclusion.cpp`](concepts/04_mutual_exclusion.cpp)

## 5. Atomicity
An operation is atomic if no other thread can observe it half done. `std::atomic<T>` gives atomic loads, stores, and read-modify-writes (`fetch_add`, `exchange`, `compare_exchange`).

Atomicity of each step does not make a *sequence* atomic. "Read balance, check ≥ amount, subtract" needs a lock or a CAS retry loop.

**Memory ordering** is the second half of the topic: `memory_order_seq_cst` (default, simplest), `acquire`/`release` (publish data safely between threads), `relaxed` (only the variable itself is atomic; fine for counters).
**Code:** [`05_atomicity.cpp`](concepts/05_atomicity.cpp) (CAS loop)

## 6. Deadlock
Deadlock requires all four **Coffman conditions** at once:

1. **Mutual exclusion** — resources can't be shared.
2. **Hold and wait** — a thread holds one resource while waiting for another.
3. **No preemption** — resources can't be taken away.
4. **Circular wait** — a cycle of threads each waiting on the next.

Break any one and deadlock is impossible. In practice:

- **Lock ordering** — always acquire locks in one global order (breaks circular wait). The most common fix.
- **Acquire all at once** — `std::scoped_lock(a, b)` / `std::lock` (breaks hold-and-wait).
- **try_lock with timeout and back-off** (adds a form of preemption — but beware livelock).
- **Detection** — build a wait-for graph and look for cycles (databases do this, then abort a victim).

**Code:** [`06_deadlock.cpp`](concepts/06_deadlock.cpp)

## 7. Livelock (and starvation)
In **livelock**, threads are running and reacting to each other but make no progress — like two people stepping aside in a corridor in the same direction forever. Commonly caused by symmetric retry logic: both release, both retry, both collide again.

**Fix:** break the symmetry — randomised or exponential back-off, or a priority/ordering rule.

**Starvation** is related: the system progresses, but one particular thread never gets the resource (e.g. writers starving behind a constant stream of readers). Fix with fairness — FIFO queues, tickets, ageing.

| | Threads blocked? | System progressing? |
|---|---|---|
| Deadlock | Yes | No |
| Livelock | No (busy) | No |
| Starvation | Victim waits | Yes, for others |

**Code:** [`07_livelock.cpp`](concepts/07_livelock.cpp)

## 8. Producer–consumer
Producers put items into a **bounded** buffer; consumers take them out. Rules: producers block when it's full, consumers block when it's empty, and buffer access is mutually exclusive.

Standard C++ solution: one mutex + two condition variables (`notFull`, `notEmpty`). Alternative: two counting semaphores (`emptySlots = N`, `fullSlots = 0`) plus a mutex.

Things interviewers probe:
- **Always wait with a predicate / in a `while` loop** — spurious wakeups and stolen wakeups are real.
- `notify_one` vs `notify_all` — one item freed → `notify_one` is enough.
- **Shutdown** — how do consumers know to stop? (a `close()` flag, or poison-pill items).
- Why bounded? Backpressure: a fast producer otherwise exhausts memory.

**Code:** [`08_producer_consumer.cpp`](concepts/08_producer_consumer.cpp) — the most important file in this repo for interviews.

## 9. Process vs thread
| | Process | Thread |
|---|---|---|
| Memory | Own address space | Shares the process's address space |
| Owns | Address space, file descriptors, PID | Own stack, registers, thread-local storage |
| Creation / switch cost | Higher (page tables, TLB flush) | Lower |
| Communication | IPC: pipes, sockets, shared memory, signals | Directly via shared memory (needs synchronisation) |
| Fault isolation | A crash is contained | One bad pointer can kill all threads |

On Linux both are created by `clone()` with different sharing flags; `fork()` copies a process lazily using copy-on-write.

**Interview angle:** Chrome uses processes per tab for isolation; servers use threads (or async) for cheap sharing.

## 10. Creating and managing threads
Threading models map user threads to kernel threads:

- **1:1** — every user thread is a kernel thread. Linux (NPTL), Windows, and `std::thread` work this way.
- **N:1** — many user threads on one kernel thread; cheap but one blocking call blocks all, no parallelism.
- **M:N** — many user threads multiplexed over a pool of kernel threads. Go goroutines, Java virtual threads.

In C++: `std::thread` starts immediately; every thread must be `join()`ed or `detach()`ed before its destructor, or the program calls `std::terminate`. Arguments are **copied** unless you wrap them in `std::ref`. C++20's `std::jthread` joins automatically and supports cooperative cancellation via `stop_token`.
**Code:** [`10_thread_creation.cpp`](concepts/10_thread_creation.cpp)

## 11. Thread pools
A thread pool keeps a fixed set of worker threads that pull tasks from a shared queue.

Why: thread creation is expensive; unbounded thread creation exhausts memory and thrashes the scheduler; a pool gives you a natural place for back-pressure, priorities, and metrics.

Sizing rule of thumb: **CPU-bound** ≈ number of cores; **I/O-bound** ≈ cores × (1 + wait time / compute time).

Design points interviewers ask about: bounded vs unbounded queue, what to do when the queue is full (block, reject, run on caller), graceful shutdown (drain vs drop), exceptions in tasks (capture into the future), and work-stealing (per-worker deques to reduce contention).
**Code:** [`11_thread_pool.cpp`](concepts/11_thread_pool.cpp)

## 12. Context switching
A context switch saves one thread's state (registers, program counter, stack pointer) and restores another's.

Triggers: the time slice expires (preemption), the thread blocks (I/O, lock, sleep), it yields, or a higher-priority thread becomes runnable.

Cost: the switch itself is a few microseconds, but the hidden cost is **cache and TLB pollution** afterwards. Process switches cost more than thread switches because the address space changes.

Implications: too many runnable threads → time spent switching instead of working; this is why pools are sized and why event loops / async I/O exist.
**Code:** [`12_context_switch.cpp`](concepts/12_context_switch.cpp)

## 13. Many small threads vs a few large ones
**Many threads, small tasks** — good load balancing and responsiveness, but more memory (each thread has its own stack, often 1–8 MB reserved), more context switches, and more lock contention.

**Few threads, large tasks** — low overhead and good cache locality, but poor balancing (one slow chunk leaves cores idle) and a long task can block others.

Middle ground in practice: a small pool of threads (≈ cores) running **many small tasks** — task granularity is fine, thread count is not. Work-stealing schedulers (TBB, Java ForkJoin, Go runtime) are built on this idea.

## 14. Mutex
A mutex is a lock with **ownership**: only the thread that locked it may unlock it. A thread that finds it locked sleeps until it is released.

C++ flavours:
- `std::mutex` — basic, non-recursive.
- `std::recursive_mutex` — same thread may lock repeatedly (usually a design smell).
- `std::timed_mutex` — `try_lock_for` / `try_lock_until`.
- `std::shared_mutex` — reader–writer lock: many readers or one writer.

Always lock through RAII: `lock_guard` (simple), `unique_lock` (needed for condition variables, can unlock early), `scoped_lock` (multiple mutexes, deadlock-safe).

Pitfalls: deadlock, **priority inversion** (a low-priority thread holds a lock a high-priority thread needs; fixed by priority inheritance), contention, and holding locks during slow operations.

## 15. Semaphore vs mutex
A **semaphore** is a counter with two atomic operations: `acquire` (wait until count > 0, then decrement) and `release` (increment, waking a waiter).

| | Mutex | Semaphore |
|---|---|---|
| Value | Locked / unlocked | Any count ≥ 0 |
| Ownership | Yes — the locker must unlock | No — any thread may release |
| Main use | Protecting a critical section | Limiting access to N resources, or signalling between threads |

A binary semaphore looks like a mutex but isn't one: without ownership it can't support priority inheritance or catch "unlocked by the wrong thread" bugs. Conversely, that lack of ownership is exactly what makes semaphores good for **signalling** (thread A releases, thread B acquires).

C++20 provides `std::counting_semaphore<N>` and `std::binary_semaphore`.
**Code:** [`15_semaphore_vs_mutex.cpp`](concepts/15_semaphore_vs_mutex.cpp)
