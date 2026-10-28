# Concurrency interview examples in C++

C++20 versions of the code examples from the first 15 questions of
[Devinterview-io/concurrency-interview-questions](https://github.com/Devinterview-io/concurrency-interview-questions).
Questions with no code example (3, 9, 13, 14) are concept-only and have no file.

## Build and run

```bash
g++ -std=c++20 -pthread -O1 08_producer_consumer.cpp -o pc && ./pc
# or everything:
cmake -B build && cmake --build build
```

Check for data races with ThreadSanitizer:

```bash
g++ -std=c++20 -pthread -fsanitize=thread -g 02_race_condition.cpp -o race && ./race
```

## Files

| File | Topic |
|---|---|
| 01_concurrency_vs_parallelism.cpp | std::thread and std::async |
| 02_race_condition.cpp | Race, then fixes with mutex and atomic |
| 04_mutual_exclusion.cpp | lock_guard (RAII) |
| 05_atomicity.cpp | Compare-and-swap loop |
| 06_deadlock.cpp | Deadlock (`./06_deadlock deadlock`) and scoped_lock fix |
| 07_livelock.cpp | Livelock and random-backoff fix (`./07_livelock fix`) |
| 08_producer_consumer.cpp | Bounded blocking queue with close() |
| 10_thread_creation.cpp | Function, lambda, member, std::ref |
| 11_thread_pool.cpp | Thread pool with futures |
| 12_context_switch.cpp | Voluntary switches and interleaving |
| 15_semaphore_vs_mutex.cpp | std::counting_semaphore |
