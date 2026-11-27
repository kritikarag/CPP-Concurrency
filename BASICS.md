# C++ concurrency basics for beginners

> Unfamiliar word? See the [glossary](GLOSSARY.md).

Every solution in this repo is built from the same set of C++ pieces. If a line in a solution looks like magic, the explanation is here. Sections 1–8 are what the beginner solutions (**Solution 1**) need; sections 9–18 are extra tools that appear in the harder problems and in the advanced **Solution 2** code.

Each solution file starts with a **"New in this file"** note pointing to the sections you need for it.

Read sections 1–8 alongside Days 1–2 of the [plan](PLAN.md).

**Contents**
1. Starting and finishing a thread · 2. mutex · 3. lock_guard (why there's no unlock line) · 4. unique_lock · 5. condition_variable · 6. atomic · 7. function<void()> · 8. How the test code in each file works
9. Time: chrono, sleep_for, wait_for / wait_until · 10. Containers: queue, priority_queue, list, unordered_map/set, pair · 11. Exceptions: try / catch / throw · 12. Files: open, pread, pwrite · 13. Lambdas and auto · 14. Templates and std::optional · 15. Ownership: std::move, unique_ptr, shared_ptr · 16. More locks: semaphore, shared_mutex, scoped_lock, try_lock · 17. Results and cancellation: future, async, packaged_task, stop_token · 18. Lock-free tools: compare_exchange and memory_order · 19. The rules every solution follows

## 1. Starting and finishing a thread
```cpp
void work(int id) { /* ... */ }

thread t(work, 7);   // starts running work(7) on a new thread RIGHT NOW
t.join();            // wait here until it finishes
```
- Every thread must be `join()`ed (or `detach()`ed) before the `thread` object is destroyed, otherwise the program crashes with `std::terminate`.
- Arguments are **copied** into the thread. To pass something by reference, write `thread t(work, ref(x));`.
- To run a member function: `thread t(&MyClass::method, this);` (the object goes second).

## 2. mutex: one thread at a time
A `mutex` is a lock. Only one thread can hold it; others trying to lock it wait.
```cpp
mutex m;
m.lock();
counter++;      // only one thread can be here at a time
m.unlock();
```
You will almost never see `m.lock()` / `m.unlock()` in this repo, because of the next section.

## 3. lock_guard: why there is no unlock line
```cpp
void push(int value) {
    lock_guard<mutex> lock(m);   // locks m
    items.push_back(value);
}                                // `lock` is destroyed here -> unlocks m
```
`lock_guard` locks the mutex in its **constructor** and unlocks it in its **destructor**. C++ guarantees a local variable's destructor runs when the function exits, by *any* path: every `return`, the closing brace, or an exception. So:

- There is no unlock line, and you can't write one: `lock_guard` has no `unlock()` method.
- A function with several `return`s still unlocks correctly on each of them.
- If the code throws (e.g. out of memory), the mutex is still released and other threads don't hang.

This idea, an object that grabs a resource when created and releases it when destroyed, is called **RAII**. It's the reason C++ code rarely leaks locks, files or memory.

**Gotchas**
- The variable needs a name. `lock_guard<mutex>(m);` (no name) does **not** lock anything; C++ reads it as declaring a new variable called `m`.
- The lock lasts until the end of the enclosing `{ }`. To hold it for only part of a function, put that part in its own braces:
  ```cpp
  {
      lock_guard<mutex> lock(m);
      task = tasks.front(); tasks.pop();
  }               // unlocked here
  task();         // runs without the lock
  ```

## 4. unique_lock: a lock_guard you can control
`unique_lock` does the same automatic unlocking, but you can also unlock and re-lock it by hand. You need it in two situations:
```cpp
unique_lock<mutex> lock(m);
while (q.empty()) cv.wait(lock);   // 1. condition variables require a unique_lock
int x = q.front(); q.pop();
lock.unlock();                     // 2. release early, before slow work
process(x);
```
Rule of thumb: use `lock_guard` unless you need `cv.wait` or an early `unlock()`; then use `unique_lock`.

## 5. condition_variable: sleeping until something changes
A condition variable lets a thread sleep until another thread says "something changed, check again".
```cpp
// waiting side
unique_lock<mutex> lock(m);
while (turn != 2) cv.wait(lock);
// ...it's my turn now, and I hold the lock...

// signalling side
{
    lock_guard<mutex> lock(m);
    turn = 2;
}
cv.notify_all();
```
What `cv.wait(lock)` does, step by step:
1. Unlocks `m` (so other threads can change `turn`).
2. Puts the thread to sleep.
3. When notified, **re-locks** `m` before returning.

**Why a `while` loop and not an `if`?** When `wait` returns, the condition might still be false:
- *Spurious wakeup*: the OS is allowed to wake a thread for no reason.
- *Stolen wakeup*: another thread may have woken first and used up the item or the turn.

The `while` re-checks the condition every time. `cv.wait(lock, [&]{ return turn == 2; });` is the same loop written as a one-liner; you'll see that form in the Solution 2 code.

**`notify_one` or `notify_all`?**
- `notify_one` wakes one waiting thread. Use it when any waiter can make progress (e.g. one item was added to a queue, so any consumer can take it).
- `notify_all` wakes every waiter. Use it when only a *specific* waiter can proceed (e.g. "it's thread 2's turn"). With `notify_one` you might wake thread 3, which goes back to sleep, and thread 2 never wakes up.

## 6. atomic: a variable that's safe without a lock
```cpp
atomic<int> counter(0);
counter++;       // safe from many threads at once, no mutex needed
```
A normal `int` isn't safe to change from two threads: `counter++` is really "read, add 1, write", and two threads can interleave those steps and lose an update. An `atomic<int>` does the whole `++` as one indivisible step in hardware.

- `x.load()` reads and `x.store(5)` writes. Plain `x` and `x = 5` do the same thing; the long names just make it obvious in code that the variable is shared.
- `atomic<bool> done{false};` is the usual way to make a flag that one thread sets and others check, like a "please stop" signal.
- Good for single counters and flags. As soon as two variables have to change together (e.g. `balance -= x` and `log.push_back(x)`), use a mutex instead. The atomics would each be safe, but another thread could see one changed and the other not yet.

## 7. function<void()>: "any piece of code to run later"
```cpp
void hello() { cout << "hi\n"; }
function<void()> job = hello;   // store it
job();                           // run it
```
`function<void()>` holds anything you can call with no arguments and no return value. Thread pools and schedulers store tasks this way. LeetCode's concurrency problems pass `printFirst` and friends the same way.

The part inside the angle brackets is the shape of the function: `function<void(int)>` takes an `int` and returns nothing (like `printNumber(5)`), and `function<int()>` takes nothing and returns an `int`.

## 8. How the test code in each file works
Every file ends with a small test, written the same way everywhere:
```cpp
Foo* foo;                                   // a GLOBAL object, so plain functions can reach it
void callFirst() { foo->first(printFirst); }

int main() {
    foo = new Foo();                        // create it
    thread t1(callFirst), t2(callSecond);   // start threads on plain functions
    t1.join(); t2.join();
    assert(output == "firstsecond");        // crash loudly if the result is wrong
    delete foo;                             // free it
    advanced::run();                        // then test Solution 2
}
```
- **Global object + plain function.** `thread` needs something to run. The simplest choice is an ordinary function, and making the shared object global lets that function use it.
- **`new` / `delete`.** `new Foo()` creates an object and gives back a pointer (`Foo*`); `foo->first(...)` calls a method through a pointer; `delete foo` frees it. Used where a test makes a fresh object several times.
- **`assert(condition)`.** If the condition is false the program stops with an error naming the line. Each test checks the thing that would break if the synchronisation were wrong, e.g. "never two writers at once".
- **`thread(&ThreadPool::workerLoop, this)`.** Starts a thread on a *member function*. A member function needs an object to run on, so you pass the function's address (`&Class::method`) and then the object (`this`, the current object). Classes use this to start their own worker threads.
- **`thread(philosopher, i)`.** Extra arguments after the function are passed to it, so this runs `philosopher(i)`.
- **`this->n = n;`** When a parameter has the same name as a member, `this->n` means "the member", plain `n` means "the parameter".
- **`using namespace std;`** lets you write `mutex` instead of `std::mutex`. Fine for interviews and practice files.
- **`namespace advanced { ... }`** Solution 2 lives inside this "name box" so its class names (e.g. a second `Foo`) don't clash with Solution 1's. `advanced::run()` calls its test.

---

## 9. Time: chrono, sleep_for, wait_for / wait_until
```cpp
this_thread::sleep_for(chrono::milliseconds(100));      // pause this thread for 100 ms

auto start = chrono::steady_clock::now();               // a point in time
auto deadline = start + chrono::milliseconds(500);      // a point 500 ms later
auto elapsed = chrono::steady_clock::now() - start;     // a duration
double seconds = chrono::duration<double>(elapsed).count();   // ...as a number of seconds
```
- **Use `steady_clock` for measuring time and timeouts.** It only ever moves forward. The wall clock (`system_clock`) can jump backwards or forwards when the computer syncs its time, which would break a timeout.
- **`cv.wait_for(lock, 200ms)` / `cv.wait_until(lock, deadline)`** are `cv.wait` with an alarm clock: sleep until notified *or* the time runs out. They return `cv_status::timeout` if time ran out, `cv_status::no_timeout` if notified. Waking doesn't mean the condition is true, so keep the `while` loop.
- `auto` just means "let the compiler work out the type" (see §13). The real types of time points are long, so `auto` is common here.

## 10. Containers: queue, priority_queue, list, unordered_map/set, pair
- **`queue<int> q;`** first in, first out. `q.push(x)` adds at the back, `q.front()` reads the oldest, `q.pop()` removes it (and returns nothing, so read `front()` first).
- **`deque<int>`** like a queue but you can add and remove at both ends.
- **`priority_queue<T>`** always gives you the *largest* item first via `top()`. To get the *smallest* (e.g. the earliest deadline) you pass a comparator that says when one item should come *after* another:
  ```cpp
  struct EarliestFirst {
      bool operator()(const Task& a, const Task& b) const { return a.runAt > b.runAt; }
  };
  priority_queue<Task, vector<Task>, EarliestFirst> tasks;   // top() = earliest task
  ```
  Read the comparator as "a goes below b if a is later". It feels backwards; everyone finds it confusing.
- **`unordered_map<int,int> m;`** a hash map: `m[key] = value`, `m.find(key) == m.end()` means "not there", `m.erase(key)` removes. **`unordered_set<string> s;`** the same but keys only: `s.insert(x)`, and `s.count(x)` is 1 if present, 0 if not.
- **`pair<int,int> p = {key, value};`** two values glued together; `p.first`, `p.second`.
- **`list<int>`** a doubly linked list. Its superpower: an **iterator** (a "pointer to one element", type `list<int>::iterator`) stays valid while other elements are added or removed. `items.begin()` is an iterator to the first element. `items.splice(items.begin(), items, it)` *moves* the element `it` points at to the front, in O(1), without copying it. That's how the LRU cache marks an item "most recently used".

## 11. Exceptions: try / catch / throw
```cpp
void task() { throw runtime_error("disk full"); }      // report an error by throwing

try {
    task();
} catch (exception& e) {          // catch anything derived from std::exception
    cout << e.what();             // "disk full"
} catch (...) {                   // catch absolutely anything else
}
```
Why it matters for threads: if an exception escapes a thread's function, C++ calls `std::terminate` and the **whole program** dies. A thread pool must therefore catch exceptions from tasks and pass them somewhere, such as an error list or a `future` (§17).

## 12. Files: open, pread, pwrite (the file-copy problem)
These are the Linux/macOS system calls, from `<fcntl.h>` and `<unistd.h>`:
- `int fd = open("a.bin", O_RDONLY);` opens a file and returns a **file descriptor**, a small number that stands for the open file. `O_RDWR | O_CREAT` means read/write and create it if missing. A negative number means it failed.
- `lseek(fd, 0, SEEK_END)` moves to the end and returns the position, i.e. the file size.
- `ftruncate(fd, size)` sets the file's length up front, so every thread can write its own part.
- **`pread(fd, buffer, length, offset)` / `pwrite(...)`** read or write `length` bytes at an exact `offset`. Plain `read`/`write` use the file's shared "current position", so two threads would fight over it and need a lock; `pread`/`pwrite` don't touch it, so threads can work on different chunks in parallel with no lock.
- `close(fd)` when done. `ifstream` / `ofstream` are the friendlier C++ file classes, used in the tests to create and compare files.

## 13. Lambdas and auto
A **lambda** is a small unnamed function written inline. You'll see them everywhere in Solution 2:
```cpp
auto add = [](int a, int b) { return a + b; };    // a function stored in a variable
add(2, 3);                                         // 5

int total = 0;
auto addToTotal = [&](int x) { total += x; };      // [&] lets it use (and change) `total`
thread t([&] { addToTotal(5); });                  // start a thread on a lambda
```
- The `[...]` part is the **capture list**: which outside variables the lambda can see. `[&]` = all of them, by reference; `[=]` = copies of them; `[i]` = a copy of just `i`; `[&, i]` = everything by reference except `i`, which is copied.
- **Capturing by reference is dangerous with threads** if the variable dies or changes before the thread uses it, e.g. a loop variable. Debugging drill 3 has exactly this bug.
- `cv.wait(lock, [&]{ return turn == 2; });` is the one-line form of `while (!(turn == 2)) cv.wait(lock);`.
- **`auto x = ...;`** asks the compiler to work out the type. Same code, less typing.

## 14. Templates and std::optional
- **Templates** let one class work for any type: `template <typename T> class Queue { ... T item; ... };` then `Queue<int>` or `Queue<string>`. The Solution 1 versions just use `int` to keep things readable.
- **`optional<int>`** means "an int, or nothing". A cleaner alternative to returning `-1` or using a `bool` plus an output parameter:
  ```cpp
  optional<int> pop() { if (empty) return nullopt; return value; }
  if (auto v = pop()) cout << *v;      // *v gets the value out
  ```

## 15. Ownership: std::move, unique_ptr, shared_ptr
- **`std::move(x)`** says "I'm done with x, take its contents instead of copying". It's used to avoid copying big objects such as strings, vectors or tasks.
- **`unique_ptr<Node>`** is a pointer that owns its object and `delete`s it automatically when it goes away: RAII again (§3), this time for memory. Only one `unique_ptr` can own an object at a time.
- **`shared_ptr<T>`** can have many owners; the object is deleted when the last one goes away. The thread pool uses it to share one task between the queue and the caller's `future`.

## 16. More locks: semaphore, shared_mutex, scoped_lock, try_lock
- **`counting_semaphore<3> s(3);`** a counter of available "permits". `s.acquire()` waits until one is free and takes it; `s.release()` gives one back. Unlike a mutex, any thread may release. **`binary_semaphore s(0)`** has 0 or 1 permits and is good for "thread A tells thread B to go", as in FooBar.
- **`shared_mutex`** is a reader–writer lock: `shared_lock<shared_mutex> r(m)` for readers (many at once), `unique_lock<shared_mutex> w(m)` for writers (alone).
- **`scoped_lock both(m1, m2);`** locks several mutexes at once without deadlocking, whatever order other threads use.
- **`m.try_lock()`** tries to lock without waiting and returns `true`/`false`. `lock_guard<mutex> g(m, adopt_lock);` means "I already locked m; just unlock it for me later".

## 17. Results and cancellation: future, async, packaged_task, stop_token
- **`future<int>`** is "an int that will exist later". `f.get()` waits for it and returns it; if the task threw, `get()` re-throws the exception in *your* thread. `f.wait_for(100ms)` checks whether it's ready without waiting forever.
- **`async(launch::async, work)`** runs `work` on another thread and hands you its `future`.
- **`packaged_task<int()>`** wraps a function so that running it fills in a `future`. Thread pools use it to give callers a result.
- **`stop_source` / `stop_token`** are a ready-made "please stop" flag (C++20). Whoever owns the `stop_source` calls `request_stop()`; the task checks `token.stop_requested()` regularly and exits. This is how you cancel work, since C++ cannot safely kill a running thread.

## 18. Lock-free tools: compare_exchange and memory_order
- **`x.compare_exchange_strong(expected, desired)`** ("CAS") means: "if x still equals `expected`, set it to `desired` and return true; otherwise copy x's current value into `expected` and return false". All in one atomic step. Typical use: claim a free slot (`expected = 0`, `desired = myTicket`) so only one thread can win it. The `_weak` version can fail for no reason, so it's only used inside retry loops.
- **`memory_order_release` / `memory_order_acquire`** guarantee that if thread B *sees* the value thread A stored, B also sees everything A wrote *before* that store. That's how a new list node is "published" safely. `memory_order_relaxed` only guarantees the variable itself is atomic, which is fine for a simple counter. If you're unsure, leave it out: the default is the safest.

## Small extras you'll see
- **`constexpr int N = 5;`** a constant known at compile time.
- **`volatile`** only stops the compiler from deleting a "useless" loop in a demo. It does **not** make anything thread-safe; that's a common misconception.
- **`emplace_back(args)`** is `push_back` that builds the element in place from its arguments.
- **`mutable mutex m;`** lets a `const` method (one that promises not to change the object) still lock the mutex.
- **`for (auto& [key, value] : items)`** ("structured binding") unpacks a pair into two named variables.
- **`virtual int read(...) = 0;`** declares a function every subclass must provide: that's an *interface* (an "abstract class"). A subclass writes `int read(...) override { ... }`; `override` makes the compiler check the signature matches. Interviews often hand you an interface like this and a test supplies a fake implementation.
- **`memcpy(dest, src, n)`** copies `n` raw bytes (from `<cstring>`).
- **`enum class State { Loading, Ready };`** a type with a fixed set of named values, used as `State::Loading`.
- **`mt19937 rng(seed);`** a random number generator; tests use it to make random but repeatable workloads.


## 19. The rules every solution follows
1. Every read or write of shared data happens while holding **the same** mutex.
2. Every `cv.wait` is inside a `while` loop that checks the condition.
3. Change the shared state **before** you notify.
4. Never do slow work (sleeping, I/O, running a user's task) while holding a lock.
5. Every thread gets joined.

If a solution confuses you, check which of these rules each line is there to satisfy.
