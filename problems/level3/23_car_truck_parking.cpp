// Thread-safe Car and Truck parking in a 1-D array (a "memory allocation" problem in disguise).
// N slots in a row. A car needs 1 free slot, a truck needs 2 ADJACENT free slots.
// Many threads park and leave concurrently.
//
// It really is a memory allocator: slots = memory, car = 1-byte alloc, truck = 2-byte aligned-ish alloc,
// and fragmentation is the core issue — cars scattered across the lot can leave many free slots
// but no free PAIR, so trucks fail even though there's space.
//
// Three versions, the order you'd present them in an interview:
//  A. Coarse lock: one mutex, first-fit search. Correct and simple. Blocking park() with timeout.
//  B. Fragmentation-aware placement: cars prefer slots that DON'T break up a free pair.
//  C. Fine-grained, lock-free slots: each slot is an atomic; a truck claims slot i then i+1 with
//     compare-and-swap and rolls back if the second claim fails.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <vector>
#include <optional>
#include <random>
#include <tuple>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - a vector<int> of slots where 0 = free and anything else = the ticket parked there
//  - wait_until + cv_status::timeout for a parking attempt with a time limit — BASICS §9

namespace advanced { void run(); }

// slots[i] == 0 means free; otherwise it holds the ticket number of the vehicle parked there.
class ParkingLot {
    vector<int> slots;
    int nextTicket;
    mutex m;
    condition_variable spaceFreed;

public:
    ParkingLot(int size) {
        slots = vector<int>(size, 0);
        nextTicket = 1;
    }

    // Returns the slot index, or -1 if there is no free slot.
    int parkCar() {
        lock_guard<mutex> lock(m);
        for (int i = 0; i < (int)slots.size(); i++) {
            if (slots[i] == 0) {
                slots[i] = nextTicket++;
                return i;
            }
        }
        return -1;
    }

    // A truck needs two ADJACENT free slots. Returns the first slot index, or -1.
    int parkTruck() {
        lock_guard<mutex> lock(m);
        for (int i = 0; i + 1 < (int)slots.size(); i++) {
            if (slots[i] == 0 && slots[i + 1] == 0) {
                slots[i] = nextTicket;
                slots[i + 1] = nextTicket;      // same ticket in both slots
                nextTicket++;
                return i;
            }
        }
        return -1;
    }

    // Blocking version: wait up to `ms` milliseconds for two adjacent slots.
    int parkTruckWaiting(int ms) {
        unique_lock<mutex> lock(m);
        auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
        while (true) {
            for (int i = 0; i + 1 < (int)slots.size(); i++) {
                if (slots[i] == 0 && slots[i + 1] == 0) {
                    slots[i] = slots[i + 1] = nextTicket++;
                    return i;
                }
            }
            // Sleep until someone leaves (notify) or the deadline passes. cv_status::timeout = time ran out (BASICS §9).
            if (spaceFreed.wait_until(lock, deadline) == cv_status::timeout) return -1;
        }
    }

    void leave(int slot, bool isTruck) {
        {
            lock_guard<mutex> lock(m);
            slots[slot] = 0;
            if (isTruck) slots[slot + 1] = 0;
        }
        spaceFreed.notify_all();
    }

    int freeSlots() {
        lock_guard<mutex> lock(m);
        int free = 0;
        for (int s : slots) if (s == 0) free++;
        return free;
    }
};

// ---------- test ----------
ParkingLot lot(10);
int waitingTruckSlot = -2;
void waitForTruckSpot() { waitingTruckSlot = lot.parkTruckWaiting(500); }

ParkingLot busyLot(40);
void driver(int seed) {
    vector<pair<int, bool>> parked;             // (slot, isTruck)
    for (int i = 0; i < 3000; i++) {
        seed = seed * 1103515245 + 12345;
        int r = (seed >> 16) & 0x7fff;
        if (!parked.empty() && r % 2 == 0) {
            busyLot.leave(parked.back().first, parked.back().second);
            parked.pop_back();
        } else if (r % 3 == 0) {
            int s = busyLot.parkTruck();
            if (s != -1) parked.push_back({s, true});
        } else {
            int s = busyLot.parkCar();
            if (s != -1) parked.push_back({s, false});
        }
    }
    for (auto p : parked) busyLot.leave(p.first, p.second);
}

int main() {
    // Fragmentation: fill with cars, then free every second slot.
    vector<int> carSlot(10);
    for (int i = 0; i < 10; i++) carSlot[i] = lot.parkCar();
    for (int i = 1; i < 10; i += 2) lot.leave(carSlot[i], false);
    assert(lot.freeSlots() == 5 && lot.parkTruck() == -1);   // 5 free, but no two side by side
    cout << "5 free slots, truck still can't park (fragmentation)\n";

    thread t(waitForTruckSpot);
    this_thread::sleep_for(chrono::milliseconds(50));
    lot.leave(carSlot[0], false);                            // now slots 0 and 1 are free
    t.join();
    assert(waitingTruckSlot == 0);
    cout << "Waiting truck parked at slot 0\n";

    vector<thread> drivers;
    for (int i = 0; i < 6; i++) drivers.push_back(thread(driver, i + 1));
    for (int i = 0; i < 6; i++) drivers[i].join();
    assert(busyLot.freeSlots() == 40);                      // everyone left: lot is empty again
    cout << "Solution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - compare_exchange_strong to claim a slot without a global lock, and rolling back on failure — BASICS §18
//  - optional, pair, structured bindings — BASICS §14, §10, extras

namespace advanced {
enum class Vehicle { Car = 1, Truck = 2 };
inline int width(Vehicle v) { return (int)v; }

// ---------------- A + B: mutex-protected ----------------
class ParkingLot {
    std::mutex m; std::condition_variable spaceFreed;
    std::vector<int> slots;                         // 0 = free, otherwise the ticket id occupying it
    int nextTicket = 1;
    bool smartPlacement;

    std::optional<int> findSpot(Vehicle v) {         // caller holds m
        int n = (int)slots.size(), w = width(v), best = -1;
        for (int i = 0; i + w <= n; ++i) {
            bool fits = true;
            for (int k = 0; k < w; ++k) fits &= slots[i + k] == 0;
            if (!fits) continue;
            if (!smartPlacement || v == Vehicle::Truck) return i;             // first fit
            // B: for a car, prefer a slot whose neighbours are taken (a "hole"), so free pairs survive.
            bool leftBusy = i == 0 || slots[i - 1] != 0, rightBusy = i == n - 1 || slots[i + 1] != 0;
            if (leftBusy && rightBusy) return i;                               // perfect hole
            if (best == -1) best = i;
        }
        return best == -1 ? std::nullopt : std::optional<int>(best);
    }
public:
    explicit ParkingLot(int n, bool smart = false) : slots(n, 0), smartPlacement(smart) {}

    std::optional<std::pair<int, int>> tryPark(Vehicle v) {    // {ticket, start slot}
        std::lock_guard lk(m);
        auto at = findSpot(v);
        if (!at) return std::nullopt;
        int t = nextTicket++;
        for (int k = 0; k < width(v); ++k) slots[*at + k] = t;
        return std::pair{t, *at};
    }
    std::optional<std::pair<int, int>> park(Vehicle v, std::chrono::milliseconds timeout) {
        std::unique_lock lk(m);
        std::optional<int> at;
        if (!spaceFreed.wait_for(lk, timeout, [&] { return (at = findSpot(v)).has_value(); }))
            return std::nullopt;
        int t = nextTicket++;
        for (int k = 0; k < width(v); ++k) slots[*at + k] = t;
        return std::pair{t, *at};
    }
    void leave(int ticket, int start, Vehicle v) {
        { std::lock_guard lk(m);
          for (int k = 0; k < width(v); ++k) { assert(slots[start + k] == ticket); slots[start + k] = 0; } }
        spaceFreed.notify_all();                    // notify_all: a freed pair may suit a truck, not the car woken first
    }
    bool invariantHolds() {                         // each ticket occupies 1 slot (car) or 2 adjacent (truck)
        std::lock_guard lk(m);
        for (size_t i = 0; i < slots.size(); ++i)
            if (slots[i] && i + 1 < slots.size() && slots[i + 1] == slots[i] && i + 2 < slots.size() && slots[i + 2] == slots[i])
                return false;
        return true;
    }
};

// ---------------- C: per-slot atomics, no global lock ----------------
class LockFreeLot {
    std::vector<std::atomic<int>> slots;
    std::atomic<int> nextTicket{1};
    std::atomic<int> scanStart{0};                  // rotate start point to spread contention
public:
    explicit LockFreeLot(int n) : slots(n) { for (auto& s : slots) s.store(0); }

    std::optional<std::pair<int, int>> tryPark(Vehicle v) {
        int t = nextTicket++, n = (int)slots.size(), base = scanStart++ % n;
        for (int j = 0; j < n; ++j) {
            int i = (base + j) % n;
            if (v == Vehicle::Truck && i + 1 >= n) continue;
            int expected = 0;
            if (!slots[i].compare_exchange_strong(expected, t)) continue;       // claim first slot
            if (v == Vehicle::Car) return std::pair{t, i};
            expected = 0;
            if (slots[i + 1].compare_exchange_strong(expected, t)) return std::pair{t, i};  // claim second
            slots[i].store(0);                                                  // roll back
        }
        return std::nullopt;
    }
    void leave(int ticket, int start, Vehicle v) {
        for (int k = 0; k < width(v); ++k) {
            int expected = ticket;
            bool ok = slots[start + k].compare_exchange_strong(expected, 0);
            assert(ok); (void)ok;
        }
    }
};

template <typename Lot>
void stress(Lot& lot, const char* name, std::atomic<int>& truckFails) {
    std::atomic<int> parks{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) ts.emplace_back([&, t] {
        std::mt19937 rng(t);
        std::vector<std::tuple<int, int, Vehicle>> mine;
        for (int i = 0; i < 4000; ++i) {
            if (!mine.empty() && rng() % 2) {
                auto [tk, at, v] = mine.back(); mine.pop_back();
                lot.leave(tk, at, v);
            } else {
                Vehicle v = rng() % 3 == 0 ? Vehicle::Truck : Vehicle::Car;
                if (auto p = lot.tryPark(v)) { mine.emplace_back(p->first, p->second, v); ++parks; }
                else if (v == Vehicle::Truck) ++truckFails;
            }
        }
        for (auto [tk, at, v] : mine) lot.leave(tk, at, v);
    });
    for (auto& t : ts) t.join();
    std::cout << name << ": " << parks << " parks, " << truckFails << " truck rejections\n";
}

void run() {
    // Fragmentation demo: cars in slots 0,2,4,... leave 5 free slots but no free pair.
    ParkingLot frag(10);
    std::vector<std::pair<int,int>> cars;
    for (int i = 0; i < 10; ++i) cars.push_back(*frag.tryPark(Vehicle::Car));
    for (int i = 1; i < 10; i += 2) frag.leave(cars[i].first, cars[i].second, Vehicle::Car);
    assert(!frag.tryPark(Vehicle::Truck));
    std::cout << "Fragmentation: 5 free slots, truck still rejected\n";

    // Blocking park: a truck waits until a pair frees up.
    std::thread waiter([&] {
        auto p = frag.park(Vehicle::Truck, std::chrono::milliseconds(500));
        assert(p && p->second == 0);
        std::cout << "Waiting truck parked at slot " << p->second << " once a pair freed\n";
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    frag.leave(cars[0].first, cars[0].second, Vehicle::Car);   // frees slots 0-1
    waiter.join();

    std::atomic<int> f1{0}, f2{0}, f3{0};
    ParkingLot firstFit(40), smart(40, true); LockFreeLot lockFree(40);
    stress(firstFit, "A. mutex, first fit       ", f1);
    stress(smart,    "B. mutex, hole-first cars ", f2);
    stress(lockFree, "C. per-slot CAS           ", f3);
    assert(firstFit.invariantHolds() && smart.invariantHolds());
    std::cout << "Solution 2 OK\n";
}
// Interview follow-ups:
//  - Truck starvation: under a steady stream of cars, a waiting truck may never see a free pair.
//    Fix: when a truck has waited too long, RESERVE a pair (mark slots as reserved so cars skip
//    them) and give it the first pair that drains. Same idea as writer-preference in RW locks.
//  - Version C's rollback: between claiming slot i and failing on i+1, slot i looks occupied
//    briefly, so another car may be turned away spuriously. Acceptable for tryPark; for a strict
//    guarantee use version A.
//  - Deadlock in C? No: CAS never blocks, and a failed truck releases what it took.
//  - Scaling: segment the lot into regions, each with its own mutex (like lock striping); trucks
//    that straddle a boundary lock both regions in index order.
//  - Generalise to vehicles of width k: the search becomes "find k contiguous free slots" —
//    exactly first-fit/best-fit memory allocation; a free-list or segment tree speeds it up.
}  // namespace advanced
