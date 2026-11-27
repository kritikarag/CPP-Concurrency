# Glossary

Every concurrency term used in this repo, in plain words. If a comment, note or answer uses a word you don't know, it should be here. For C++ syntax (lock_guard, lambdas, atomic…) see [BASICS.md](BASICS.md); for the longer explanations see [THEORY.md](THEORY.md).

## Core ideas
- **Thread** — an independent sequence of instructions running inside your program. Threads share the program's memory, which is why they can step on each other.
- **Process** — a running program with its own private memory. Threads live inside processes.
- **Thread-safe** — code that still behaves correctly when many threads call it at the same time.
- **Shared state** — data that more than one thread reads or writes. Every concurrency bug involves it.
- **Critical section** — the lines of code that touch shared state and must not be run by two threads at once.
- **Race condition** — the result depends on the exact timing of threads. The classic example is two threads doing `count++` and one increment getting lost.
- **Data race** — a specific race in C++: two threads touch the same variable with no synchronisation and at least one writes. It's **undefined behaviour**: the program may do anything, not just give a wrong number.
- **Undefined behaviour (UB)** — code the C++ standard places no requirements on. It might work, crash, or silently corrupt data, and it can change between runs or compilers.
- **Atomic / atomicity** — an operation that happens all at once: no other thread can ever see it half done.
- **Synchronisation** — any mechanism that controls the order or overlap of threads: mutexes, condition variables, semaphores, atomics.
- **Contention** — many threads wanting the same lock at the same time, so most of them wait. High contention makes programs slow.
- **Throughput** — how much work gets done per second.
- **Concurrency vs parallelism** — concurrency: tasks overlap in time, possibly by taking turns on one core. Parallelism: they literally run at the same instant on different cores.

## Things that go wrong
- **Deadlock** — threads wait for each other in a circle and none can ever continue. Example: A holds lock 1 and wants lock 2; B holds lock 2 and wants lock 1.
- **Coffman conditions** — the four conditions that must *all* hold for deadlock: mutual exclusion, hold-and-wait, no preemption, circular wait. Break any one and deadlock can't happen.
- **Livelock** — threads keep reacting to each other and retrying, so they're busy but make no progress, like two people sidestepping in a corridor.
- **Starvation** — the system makes progress but one particular thread never gets its turn, e.g. a writer that waits forever because readers keep arriving.
- **Spurious wakeup** — a thread waiting on a condition variable wakes up even though nobody notified it. The OS is allowed to do this; it's why waits sit in `while` loops.
- **Stolen wakeup** — a thread is woken because the condition became true, but another thread grabs the item or turn first. By the time it runs, the condition is false again.
- **Lost wakeup** — a notify happens when nobody is waiting yet, or wakes the wrong thread, so the thread that needed it sleeps forever.
- **Priority inversion** — a high-priority thread waits for a lock held by a low-priority thread, which itself can't run because medium-priority threads keep the CPU busy. **Priority inheritance** fixes it by temporarily boosting the lock holder.
- **Thundering herd** — `notify_all` wakes many threads but only one can proceed, so the rest wake up for nothing and go back to sleep, wasting CPU.
- **Fragmentation** — free space exists but is split into pieces too small to use, e.g. 5 free parking slots but no two side by side for a truck.

## Tools and techniques
- **Mutex (mutual exclusion lock)** — a lock only one thread can hold at a time. The holder is its **owner** and must be the one to unlock it.
- **Recursive mutex** — a mutex the *same* thread can lock several times (and must unlock as many times). A normal mutex locked twice by one thread deadlocks it. Usually a sign of messy design.
- **Reentrant** — safe to call again while already in the middle of running, e.g. from inside itself.
- **Semaphore** — a counter of permits. `acquire` takes one (waiting if none are left), `release` gives one back. No owner: any thread can release.
- **Condition variable** — lets threads sleep until another thread signals that something they care about has changed.
- **Reader–writer lock** — many readers may hold it at once, but a writer needs it alone.
- **Barrier** — a meeting point: no thread passes until all N have arrived.
- **Lightswitch** — a reader–writer pattern from *The Little Book of Semaphores*: the first reader in "turns on the light" (locks out writers) and the last reader out "turns it off".
- **Turnstile** — a semaphore everyone passes through one at a time; used to make waiting fair (first come, first served).
- **Lock ordering** — everyone acquires locks in the same global order, e.g. lower number first. Prevents circular wait, and therefore deadlock.
- **Lock striping** — instead of one lock for a whole table, use a fixed set of locks, each guarding a slice of it. Threads only collide when they hit the same slice.
- **Sharding** — splitting data into independent parts (shards), each with its own lock, so threads on different shards never block each other.
- **Back-off** — after failing to get a resource, wait a little (often a random amount, or longer each time) before retrying. Breaks livelock.
- **Backpressure** — a full, bounded queue makes fast producers wait, so they can't flood memory faster than consumers can keep up.
- **Poison pill** — a special item put in a queue that tells the consumer that takes it to shut down.
- **Thread pool** — a fixed set of threads that take tasks from a queue, so you don't pay to create a thread per task.
- **Work-stealing** — each worker has its own task queue, and an idle worker steals tasks from a busy worker's queue. Used by fast schedulers.
- **Idempotent** — doing it twice has the same effect as doing it once. Copying the same bytes to the same place again is idempotent, which makes retries safe.
- **RAII ("Resource Acquisition Is Initialization")** — an object grabs a resource (lock, memory, file) when created and releases it automatically when destroyed. `lock_guard` is the classic example.
- **First-fit** — an allocation strategy: scan from the start and take the first free space big enough.
- **Topological sort (Kahn's algorithm)** — ordering tasks so each comes after everything it depends on. Kahn's version repeatedly takes tasks with zero remaining dependencies.

## Lock-free and memory
- **Lock-free** — threads coordinate using only atomic operations, never blocking on a lock. Hard to get right; usually only for small, hot structures.
- **CAS (compare-and-swap)** — an atomic "if the value is still X, change it to Y". The building block of lock-free code. In C++: `compare_exchange_strong` / `compare_exchange_weak`.
- **ABA problem** — a CAS-based bug: a thread reads value A, another thread changes it to B and back to A, and the first thread's CAS succeeds even though things changed underneath it.
- **Hazard pointers** — a technique for lock-free structures: a thread announces which node it is using, so no other thread frees that node yet. One fix for ABA and use-after-free.
- **Treiber stack** — the classic lock-free stack, built with CAS on the head pointer.
- **Memory ordering / happens-before** — modern CPUs and compilers may reorder memory operations. A "happens-before" relationship (created by a mutex, or by atomics with release/acquire) guarantees that one thread's writes are visible to another in the right order.
- **Memory fence (barrier)** — an instruction that stops the CPU from reordering memory operations across it. Mutexes and atomics insert these for you.
- **Cache coherence** — the hardware protocol that keeps each CPU core's cache consistent with the others; it's what makes atomic operations work across cores.
- **Peterson's algorithm** — a classic way to get mutual exclusion for two threads using only ordinary reads and writes. It's correct on paper but needs memory fences on modern CPUs because of reordering.

## Operating system
- **Context switch** — the OS pausing one thread and resuming another, saving and restoring their registers. Costs time, plus cache warm-up afterwards.
- **Time slice / preemption** — each thread runs for a short slice of time before the OS forcibly switches (preempts) it so others get a turn.
- **TLB (translation lookaside buffer)** — a CPU cache of memory address translations. Switching *processes* flushes it, which is one reason process switches cost more than thread switches.
- **Futex ("fast userspace mutex")** — how Linux implements mutexes: locking an uncontended mutex is one atomic instruction; only when threads must wait does the OS kernel get involved.
- **Copy-on-write** — after `fork()`, parent and child share memory pages until one of them writes, at which point that page is copied.
- **User threads vs kernel threads (1:1, N:1, M:N)** — how a language's threads map onto the OS's threads. `std::thread` is 1:1 (each is a real OS thread). Go's **goroutines** and Java's **virtual threads** are M:N: many cheap threads multiplexed over a few OS threads.
- **Amdahl's law** — the speed-up from adding cores is capped by the part of the work that must run serially. If 10% is serial, you can never go more than 10× faster.

## Named problems (from *The Little Book of Semaphores*)
- **Producer–consumer** — producers add items to a shared buffer, consumers remove them.
- **Readers–writers** — many readers or one writer.
- **Dining philosophers** — five threads, five shared forks, each needs two. The standard deadlock example.
- **Sleeping barber** — a producer–consumer variant where customers leave if the waiting room is full.
- **Hilzer's barbershop** — the sleeping barber with several barbers and a sofa; a harder follow-up.
- **Search–insert–delete** — readers–writers with three kinds of thread.
