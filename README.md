# C++ Concurrency Interview Prep

A complete, self-contained prep kit for multithreading and concurrency interviews in C++20: theory notes, 29 practice problems with tested solutions ordered from easy to hard, timed debugging drills, and a day-by-day two-week plan.

Every solution compiles cleanly and passes **ThreadSanitizer**, and each `main()` checks its own correctness with assertions, so you can run a file and see `OK`, or break something and see it fail.

## Start here
0. **[BASICS.md](BASICS.md)** — new to C++ threads? Start here: `lock_guard` and why there's no unlock line, `unique_lock`, condition variables, and why waits are always in a `while` loop.
   **[GLOSSARY.md](GLOSSARY.md)** — every concurrency term used in the repo (deadlock, starvation, CAS, ABA, RAII…) in plain words.
1. **[PLAN.md](PLAN.md)** — the two-week schedule. Follow it top to bottom if you're starting from zero.
2. **[THEORY.md](THEORY.md)** — concept notes (races, deadlock, mutexes vs semaphores, thread pools…) with interview angles.
3. **The problems below** — write your own version first, then compare.

## Build and run
```bash
# one file
g++ -std=c++20 -pthread -Wall problems/level2/08_bounded_blocking_queue.cpp -o bbq && ./bbq

# the same with ThreadSanitizer (do this for everything you write)
g++ -std=c++20 -pthread -fsanitize=thread -g problems/level2/08_bounded_blocking_queue.cpp -o bbq && ./bbq

# everything
cmake -B build -DTSAN=ON && cmake --build build -j
```
Needs GCC 11+ or Clang 14+ (C++20 for `std::counting_semaphore`). On macOS, use Clang; on Windows, use WSL.

## Concepts
Small programs demonstrating one idea each. Notes for each are in [THEORY.md](THEORY.md).

| # | Topic | File |
|---|---|---|
| 1 | Concurrency vs parallelism, `std::thread`, `std::async` | [concepts/01_concurrency_vs_parallelism.cpp](concepts/01_concurrency_vs_parallelism.cpp) |
| 2 | Race condition, fixed with a mutex and an atomic | [concepts/02_race_condition.cpp](concepts/02_race_condition.cpp) |
| 4 | Mutual exclusion with RAII locks | [concepts/04_mutual_exclusion.cpp](concepts/04_mutual_exclusion.cpp) |
| 5 | Atomicity and compare-and-swap loops | [concepts/05_atomicity.cpp](concepts/05_atomicity.cpp) |
| 6 | Deadlock (`./x deadlock` to hang it) and `scoped_lock` fix | [concepts/06_deadlock.cpp](concepts/06_deadlock.cpp) |
| 7 | Livelock and random back-off (`./x fix`) | [concepts/07_livelock.cpp](concepts/07_livelock.cpp) |
| 8 | Producer–consumer basics | [concepts/08_producer_consumer.cpp](concepts/08_producer_consumer.cpp) |
| 10 | Creating threads: functions, lambdas, members, `std::ref` | [concepts/10_thread_creation.cpp](concepts/10_thread_creation.cpp) |
| 11 | Thread pool returning futures | [concepts/11_thread_pool.cpp](concepts/11_thread_pool.cpp) |
| 12 | Context switching | [concepts/12_context_switch.cpp](concepts/12_context_switch.cpp) |
| 15 | Semaphore vs mutex | [concepts/15_semaphore_vs_mutex.cpp](concepts/15_semaphore_vs_mutex.cpp) |

## Problems

Every problem file contains **two solutions**:
- **Solution 1 — beginner-friendly.** A plain class with `while (...) cv.wait(lock)`, simple names, and a test built from ordinary functions. Read and write this one first; it's also the style to use in an interview.
- **Solution 2 — advanced.** The same ideas with more idiomatic C++ (RAII helpers, lambdas, templates, `std::optional`), plus extra variants and follow-up discussion.

Running a file tests both and prints `Solution 1 OK` and `Solution 2 OK`.

### Level 1 — Ordering threads
| # | Problem | Key idea | Practice online |
|---|---|---|---|
| 1 | [Print in Order](problems/level1/01_print_in_order.cpp) | Stage counter + CV | [LC 1114](https://leetcode.com/problems/print-in-order/) |
| 2 | [Print FooBar Alternately](problems/level1/02_foobar_alternate.cpp) | Baton-passing semaphores | [LC 1115](https://leetcode.com/problems/print-foobar-alternately/) |
| 3 | [Odd–Even Printer](problems/level1/03_odd_even.cpp) | Parity predicate | — |
| 4 | [Print Zero Even Odd](problems/level1/04_zero_even_odd.cpp) | Three semaphores | [LC 1116](https://leetcode.com/problems/print-zero-even-odd/) |
| 5 | [FizzBuzz Multithreaded](problems/level1/05_fizzbuzz_multithreaded.cpp) | Why `notify_one` hangs | [LC 1195](https://leetcode.com/problems/fizz-buzz-multithreaded/) |

### Level 2 — Classic synchronisation
| # | Problem | Key idea | Practice online |
|---|---|---|---|
| 6 | [Building H2O](problems/level2/06_building_h2o.cpp) | Group formation | [LC 1117](https://leetcode.com/problems/building-h2o/) |
| 7 | [Thread-safe Stack](problems/level2/07_thread_safe_stack.cpp) | Why `top()`+`pop()` races | — |
| 8 | [Bounded Blocking Queue](problems/level2/08_bounded_blocking_queue.cpp) | Two CVs, semaphores, `close()` | [LC 1188](https://leetcode.com/problems/design-bounded-blocking-queue/) (Premium) |
| 9 | [Reader–Writer Lock](problems/level2/09_reader_writer_lock.cpp) | Reader vs writer preference, starvation | — |
| 10 | [Dining Philosophers](problems/level2/10_dining_philosophers.cpp) | Lock ordering | [LC 1226](https://leetcode.com/problems/the-dining-philosophers/) |
| 11 | [Party Bathroom](problems/level2/11_party_bathroom.cpp) | FIFO vs bypass limit | — |
| 12 | [Sleeping Barber](problems/level2/12_sleeping_barber.cpp) | Balking producer–consumer | — |
| 13 | [Thread-safe LRU Cache](problems/level2/13_thread_safe_lru_cache.cpp) | Sharding; why RW locks don't help | [LC 146](https://leetcode.com/problems/lru-cache/) |
| 14 | [Cyclic Barrier](problems/level2/14_cyclic_barrier.cpp) | Generation counter | — |
| 15 | [Search–Insert–Delete](problems/level2/15_search_insert_delete.cpp) | Three-way RW, atomic publish | — |
| 29 | [Shared Playground](problems/level2/29_shared_playground.cpp) | One team at a time, cap 10, no starvation (tickets vs turns) | — |

### Level 3 — System coding
| # | Problem | Key idea | Practice online |
|---|---|---|---|
| 16 | [Token-bucket Rate Limiter](problems/level3/16_token_bucket_rate_limiter.cpp) | Lazy refill, sleep outside lock | — |
| 17 | [Delayed Task Scheduler](problems/level3/17_delayed_task_scheduler.cpp) | Min-heap + `wait_until` | — |
| 18 | [Multithreaded Web Crawler](problems/level3/18_multithreaded_web_crawler.cpp) | Termination via in-flight count | [LC 1242](https://leetcode.com/problems/web-crawler-multithreaded/) (Premium) |
| 19 | [Job Scheduler with Dependencies](problems/level3/19_job_scheduler_dag.cpp) | Parallel topological sort | — |
| 20 | [Parallel File Copy](problems/level3/20_parallel_file_copy.cpp) | Chunks, checksums, resume, retries | — |
| 21 | [Striped Concurrent HashMap](problems/level3/21_striped_concurrent_hashmap.cpp) | Lock striping, safe resize | — |
| 22 | [Thread Pool Executor](problems/level3/22_thread_pool_executor.cpp) | Exceptions, timeouts, reclaiming idle threads | — |
| 23 | [Car & Truck Parking (1-D)](problems/level3/23_car_truck_parking.cpp) | Allocation, fragmentation, CAS claim + rollback | — |
| 25 | [Request Counter over a Time Window](problems/level3/25_request_counter_time_window.cpp) | Sliding window, injected clock, sharding, clean shutdown of a background thread | — |
| 30 | [Tasks across Worker Nodes](problems/level3/30_worker_node_tasks.cpp) | Per-node queues, rebalancing by load, work stealing, shutdown order | — |

### Level 3b — Memory management and file systems
Systems-coding questions often go beyond threads into memory management and file systems. These three combine both.

| # | Problem | Key idea | Practice online |
|---|---|---|---|
| 26 | [`cp` on a Custom File Interface](problems/level3/26_cp_custom_file_interface.cpp) | Short reads/writes, reader–writer pipeline, error shutdown, buffer pool | — |
| 27 | [Page Cache / Logical Memory Unit](problems/level3/27_page_cache.cpp) | Page faults without holding the lock, no double loads, LRU eviction, dirty write-back | — |
| 28 | [Backup Dependency Graph](problems/level3/28_backup_dependency_graph.cpp) | Recovery chains, cascading expiry, hand-built reader–writer lock | — |

## Debugging drills
Intentionally buggy programs for "fix this code" rounds. Timebox each, then check [debugging/ANSWERS.md](debugging/ANSWERS.md).

| Drill | Bugs | Timebox |
|---|---|---|
| [Producer–consumer](debugging/01_buggy_producer_consumer.cpp) | 4 | 20 min |
| [Bank transfers](debugging/02_buggy_bank.cpp) | 3 | 20 min |
| [Thread pool](debugging/03_buggy_thread_pool.cpp) | 4 | 25 min |

## Where else to practise
- [LeetCode Concurrency](https://leetcode.com/problemset/concurrency/) — the online judge for the LC problems above.
- [The Little Book of Semaphores](https://greenteapress.com/wp/semaphores/) — free; the best bank of synchronisation puzzles.
- [The Deadlock Empire](https://deadlockempire.github.io/) — a game where you play the scheduler and break concurrent code.
- [CoderPad sandbox](https://app.coderpad.io/sandbox) — practise in the same editor many interviews use.
- [cppreference: Concurrency support](https://en.cppreference.com/w/cpp/thread) — the API reference.
- *C++ Concurrency in Action*, Anthony Williams (2nd ed.) — chapters 2–4 cover everything here.

## Credits
The concept topics follow the first 15 questions of [Devinterview-io/concurrency-interview-questions](https://github.com/Devinterview-io/concurrency-interview-questions); the C++ code and notes are original.
