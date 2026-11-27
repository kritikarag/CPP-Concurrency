# Debugging drill answers

> Unfamiliar word? See the [glossary](../GLOSSARY.md).

Try each drill under a timer first. For every bug, name the **symptom**, the **cause**, and the **fix**; that's how to narrate it out loud in the interview.

## Drill 1 — buggy producer–consumer

1. **`if` instead of a predicate wait (push and pop).** A thread can wake spuriously, or another thread can grab the slot first (a "stolen wakeup"), so after `wait` returns the condition may still be false. Push then overfills the queue; pop calls `front()` on an empty queue (crash). Fix: `cv.wait(lk, [&]{ return q.size() < cap || closed; })` and the equivalent for pop.
2. **One condition variable for two conditions with `notify_one`.** A consumer's `notify_one` can wake another *consumer* instead of a blocked producer; that consumer goes back to sleep and the producer never wakes, so progress can stall. Fix: two CVs (`notFull`, `notEmpty`), or `notify_all`.
3. **`close()` writes `closed` without the mutex.** A data race (undefined behaviour), and consumers can check `closed`, decide to sleep, and miss the update. Fix: set it under the lock.
4. **`close()` doesn't notify.** Consumers asleep on an empty queue never wake, so `join()` hangs. Fix: `notify_all()` after setting `closed`.

## Drill 2 — buggy bank

1. **Deadlock from lock ordering.** Thread A transfers 1→2 (locks 1 then 2) while B transfers 2→1 (locks 2 then 1). Fix: lock in a fixed order (`min` id first) or use `std::scoped_lock(a, b)`.
2. **Self-transfer locks the same mutex twice.** When `from == to`, the second `lock_guard` locks a mutex the thread already holds, which is undefined behaviour and in practice a self-deadlock. Fix: `if (from == to) return;` before locking. (`scoped_lock` with the same mutex twice is also UB.)
3. **`audit()` reads balances without locks.** It races with transfers (UB) and can observe money mid-flight: debited from one account, not yet credited to the other. Fix: lock **all** accounts in index order for the audit (a consistent snapshot), or protect the bank with a `shared_mutex` where transfers take it shared plus their two account locks and audit takes it exclusive.

## Drill 3 — buggy thread pool

1. **Loop variable captured by reference: `[&]`.** Tasks run after the loop has moved on (or ended), so they read the wrong `i`, or a dead variable. Fix: capture `i` by value: `[&out, i]`.
2. **`job()` runs while holding the lock.** Only one task can run at a time, defeating the pool. Fix: pop under the lock, `lk.unlock()`, then run the job.
3. **Destructor sets `stop` without the lock and never notifies.** Idle workers sleep forever, so `join()` hangs, and the unlocked write is a data race. Fix: `{ lock_guard lk(m); stop = true; } cv.notify_all();`
4. **`if (stop) return;` drops queued work.** When stopping, workers exit even though tasks remain. Fix: `if (stop && tasks.empty()) return;`. (Whether to drain or drop on shutdown is a design choice; say which one you chose and why.)

## How to practise with these
- Run with `-fsanitize=thread`: TSan finds the data races (drill 1 bug 3, drill 2 bug 3, drill 3 bug 3), but **not** the deadlocks, lost wakeups or logic bugs. You still have to reason about those.
- Once fixed, break your own solutions in `problems/` the same way: remove a predicate, swap a lock order, notify before unlocking state, and see whether you can spot it a day later.
