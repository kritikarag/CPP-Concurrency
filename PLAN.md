# Two-week C++ concurrency plan

> Unfamiliar word? See the [glossary](GLOSSARY.md).

Written for someone starting from zero and aiming at a concurrency/multithreading interview round: system coding plus a "fix the buggy program" round, usually in a shared editor like CoderPad where the code must compile and run.

Budget: about 3–4 hours a day. Each day has **learn**, **build**, and **check yourself**. Don't open a solution file until you've written your own version and run it.

**Every day, for every program you write:**
- Compile with `g++ -std=c++20 -pthread -Wall -fsanitize=thread -g file.cpp && ./a.out`
- Run it several times. Concurrency bugs are intermittent; one clean run proves little.
- Answer the three standing questions out loud: *What if a thread wakes spuriously? Can anything starve? What changes with N threads?*

---

## Week 1 — Foundations and the classic problems

### Day 1 — Threads and races
- **Learn:** [BASICS.md](BASICS.md) §1–3 first, then [THEORY.md](THEORY.md) §1, §2, §9, §10. Process vs thread, what a race is, why `counter++` isn't atomic, why a data race is undefined behaviour in C++.
- **Build:** run [`concepts/01`](concepts/01_concurrency_vs_parallelism.cpp), [`concepts/10`](concepts/10_thread_creation.cpp) and [`concepts/02`](concepts/02_race_condition.cpp). Then, without looking, write a program where 4 threads increment a counter 1M times each; see it come out wrong; fix it with a mutex, then with `std::atomic`.
- **Check yourself:** Why must every `std::thread` be joined or detached? What does `std::ref` do and when do you need it?

### Day 2 — Locks, condition variables, semaphores
- **Learn:** [BASICS.md](BASICS.md) §4–8, then THEORY §3, §4, §5, §14, §15. `lock_guard` vs `unique_lock` vs `scoped_lock`. Condition variables, spurious wakeups, and why `wait` takes a predicate. Counting vs binary semaphores.
- **Build:** run [`concepts/04`](concepts/04_mutual_exclusion.cpp), [`05`](concepts/05_atomicity.cpp), [`15`](concepts/15_semaphore_vs_mutex.cpp). Write a "ping-pong" pair of threads twice: once with a mutex + CV + bool, once with two `binary_semaphore`s.
- **Check yourself:** Why does `cv.wait` need a `unique_lock` and not a `lock_guard`? Is a binary semaphore a mutex? (No: no ownership.)

### Day 3 — Ordering threads (Level 1, part 1)
- **Learn:** THEORY §6, §7. Deadlock (four Coffman conditions), livelock, starvation. Run [`concepts/06`](concepts/06_deadlock.cpp) with and without `deadlock`, and [`concepts/07`](concepts/07_livelock.cpp) with and without `fix`.
- **Build:** [Print in Order](problems/level1/01_print_in_order.cpp), [FooBar Alternately](problems/level1/02_foobar_alternate.cpp), [Odd–Even](problems/level1/03_odd_even.cpp). Submit the first two on LeetCode (1114, 1115).
- **Check yourself:** Solve Print in Order a second way (semaphores or `std::promise`).

### Day 4 — Ordering threads (Level 1, part 2)
- **Build:** [Zero Even Odd](problems/level1/04_zero_even_odd.cpp), [FizzBuzz Multithreaded](problems/level1/05_fizzbuzz_multithreaded.cpp). Submit on LeetCode (1116, 1195). Generalise Odd–Even to K threads printing round-robin.
- **Check yourself:** In FizzBuzz, why does `notify_one` hang? Explain the lost wakeup in one sentence.

### Day 5 — Bounded blocking queue (full day)
The single most important problem. Expect it, or a variant, in the interview.
- **Learn:** [THEORY §8](THEORY.md#8-producerconsumer); *C++ Concurrency in Action* ch. 4.1; *Little Book of Semaphores* §4.1.
- **Build:** [Bounded Blocking Queue](problems/level2/08_bounded_blocking_queue.cpp), all of it, from a blank file:
  1. one mutex + one CV with `notify_all`;
  2. one mutex + two CVs (`notFull`, `notEmpty`);
  3. two counting semaphores + a mutex;
  4. add `close()` so consumers exit cleanly, then `try_dequeue_for(timeout)`.
- **Check yourself:** In the semaphore version, what happens if you take the mutex *before* `emptySlots.acquire()`? Why notify after unlocking? Why bound the queue at all?

### Day 6 — Reader–writer locks (full day)
- **Learn:** THEORY §14 (`shared_mutex`); *Concurrency in Action* ch. 3.3.2; *Little Book of Semaphores* §4.2–4.2.6.
- **Build:** [Reader–Writer Lock](problems/level2/09_reader_writer_lock.cpp): reader-preferring first, demonstrate writer starvation, then writer-preferring, then swap in `std::shared_mutex`. Then [Search–Insert–Delete](problems/level2/15_search_insert_delete.cpp), a three-way variant.
- **Check yourself:** Why does naive lock upgrade (shared → exclusive) deadlock when two readers try it at once?

### Day 7 — Classic synchronisation, part 1
- **Build:** [Building H2O](problems/level2/06_building_h2o.cpp) (LeetCode 1117), [Dining Philosophers](problems/level2/10_dining_philosophers.cpp) (LeetCode 1226), [Thread-safe Stack](problems/level2/07_thread_safe_stack.cpp).
- **Check yourself:** Name all three dining-philosopher fixes and which Coffman condition each breaks. Why is `top()` then `pop()` racy even if both lock?
- **Review:** reread your Week 1 code. Rewrite the blocking queue from memory in under 15 minutes.

---

## Week 2 — System coding

### Day 8 — Classic synchronisation, part 2
- **Build:** [Sleeping Barber](problems/level2/12_sleeping_barber.cpp), [Party Bathroom](problems/level2/11_party_bathroom.cpp) (both FIFO and bypass-limit versions), [Cyclic Barrier](problems/level2/14_cyclic_barrier.cpp).
- **Then:** [Shared Playground](problems/level2/29_shared_playground.cpp), the bathroom problem generalised to many teams and capacity 10.
- **Check yourself:** In the barrier, what goes wrong without the generation counter? How does the playground's "turns" rule bound how long a team waits?

### Day 9 — Thread pool and LRU cache
- **Learn:** THEORY §11, §12, §13 (pools, context switches, granularity).
- **Build:** [`concepts/11_thread_pool.cpp`](concepts/11_thread_pool.cpp) from scratch, returning `std::future`s. Then [Thread-safe LRU Cache](problems/level2/13_thread_safe_lru_cache.cpp): single lock, then sharded.
- **Then:** [Thread Pool Executor](problems/level3/22_thread_pool_executor.cpp), with the classic follow-ups: a throwing task, a task with a timeout, and a pool that grows under load and reclaims idle threads.
- **Check yourself:** Why doesn't `shared_mutex` help an LRU cache? Drain vs drop on pool shutdown? Why can't you just kill a thread that times out?

### Day 10 — Time-based problems
- **Build:** [Token-bucket Rate Limiter](problems/level3/16_token_bucket_rate_limiter.cpp), [Delayed Task Scheduler](problems/level3/17_delayed_task_scheduler.cpp).
- **Then:** [Request Counter over a Time Window](problems/level3/25_request_counter_time_window.cpp), a common systems-coding question. Write Solution 1 from a blank file in 30 minutes.
- **Check yourself:** Why `steady_clock`, not `system_clock`? In the scheduler, what happens when an earlier task arrives while the dispatcher sleeps on a later deadline?

### Day 11 — Coordinating dynamic work
- **Build:** [Multithreaded Web Crawler](problems/level3/18_multithreaded_web_crawler.cpp) (LeetCode 1242), [Job Scheduler with Dependencies](problems/level3/19_job_scheduler_dag.cpp).
- **Then:** [Tasks across Worker Nodes](problems/level3/30_worker_node_tasks.cpp): rebalancing, work stealing, and why shutdown order matters.
- **Check yourself:** Why is "queue is empty" not a correct termination test? How does the DAG scheduler detect a cycle? When does moving a task from the busiest node NOT help?

### Day 12 — Data-heavy problems
- **Build:** [Parallel File Copy](problems/level3/20_parallel_file_copy.cpp) (integrity check, retries, resume), [Striped Concurrent HashMap](problems/level3/21_striped_concurrent_hashmap.cpp).
- **Then:** [Car & Truck Parking](problems/level3/23_car_truck_parking.cpp), a "memory allocation" problem in disguise. Write the single-mutex version first, then the per-slot CAS version.
- **Check yourself:** Why `pread`/`pwrite` instead of `seek` + `read`? Why must resize take every stripe lock, and in what order? How does a truck starve in the parking lot, and how would you reserve a pair for it?

### Day 12½ — Memory management and file systems
Fit this in over days 12–13; systems-coding rounds often go beyond threads into memory and files.
- **Build:** [`cp` on a Custom File Interface](problems/level3/26_cp_custom_file_interface.cpp), [Page Cache](problems/level3/27_page_cache.cpp), [Backup Dependency Graph](problems/level3/28_backup_dependency_graph.cpp).
- **Check yourself:** What does a short write mean, and what's the bug if you ignore it? In the page cache, why put a "loading" placeholder in the map before unlocking? Why can't a dirty page's frame be reused before its write-back finishes? In the backup graph, why does expiry need a queue rather than one pass?

### Day 13 — Debugging round + OS revision
- **Drills:** [`debugging/`](debugging/): drill 1 (20 min), drill 2 (20 min), drill 3 (25 min), each under a timer, narrating out loud. Then check [ANSWERS.md](debugging/ANSWERS.md).
- **Play:** [The Deadlock Empire](https://deadlockempire.github.io/): break the programs by stepping threads yourself.
- **Revise OS MCQ topics:** scheduling algorithms, process states, context switch cost, user vs kernel threads, Peterson's algorithm, priority inversion, memory barriers, `fork()` and copy-on-write.

### Day 14 — Two timed mocks
- **Mock A (45 min, build):** on [CoderPad's sandbox](https://app.coderpad.io/sandbox), from a blank file, write one problem you haven't redone this week: the delayed scheduler or the job scheduler. Talk through design first (5 min), code (30 min), test with threads (10 min).
- **Mock B (45 min, debug):** take one of your Level 2 solutions, ask a friend (or Claude) to plant 3–4 bugs without telling you where, then fix it.
- **Last pass:** reread [THEORY.md](THEORY.md) and the "Check yourself" questions above. If you can answer them all out loud, you're ready.
