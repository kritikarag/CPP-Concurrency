// Sleeping Barber
// One barber, a waiting room with N chairs. If no customers, the barber sleeps.
// A customer arriving to a full waiting room leaves. Otherwise they wait, and the barber cuts hair one at a time.
// This is producer-consumer where the producer gives up instead of blocking when full.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <vector>
#include <queue>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - unique_lock released in the middle (lock.unlock() ... lock.lock()) so the slow haircut runs without holding the lock — BASICS §4
//  - two condition variables: one for the barber, one for customers — BASICS §5

namespace advanced { void run(); }

class BarberShop {
    mutex m;
    condition_variable barberCv;    // barber sleeps here when no customers
    condition_variable customerCv;  // customers wait here for their haircut to finish
    int chairs;
    int waitingCustomers;
    int haircutsGiven;              // ticket-style: customer k is done once haircutsGiven > k
    int nextCustomerNumber;
    bool closed;

public:
    int served = 0, turnedAway = 0;

    BarberShop(int chairs) {
        this->chairs = chairs;
        waitingCustomers = 0;
        haircutsGiven = 0;
        nextCustomerNumber = 0;
        closed = false;
    }

    void customer() {
        unique_lock<mutex> lock(m);
        if (waitingCustomers == chairs) {           // no seat: leave without waiting
            turnedAway++;
            return;
        }
        int myNumber = nextCustomerNumber;
        nextCustomerNumber++;
        waitingCustomers++;
        barberCv.notify_one();                      // wake the barber if he's asleep
        while (haircutsGiven <= myNumber) customerCv.wait(lock);
    }

    void barber() {
        while (true) {
            unique_lock<mutex> lock(m);
            while (waitingCustomers == 0 && !closed) barberCv.wait(lock);   // sleep
            if (waitingCustomers == 0 && closed) return;
            waitingCustomers--;                     // take the next customer to the chair
            lock.unlock();                          // give the lock back during the slow part...
            this_thread::sleep_for(chrono::microseconds(300));               // cut hair (no lock held)
            lock.lock();                            // ...and take it again to update shared state (BASICS §4)
            haircutsGiven++;
            served++;
            customerCv.notify_all();
        }
    }

    void close() {
        lock_guard<mutex> lock(m);
        closed = true;
        barberCv.notify_all();
    }
};

// ---------- test ----------
BarberShop shop(3);
void runBarber()   { shop.barber(); }
void runCustomer() { shop.customer(); }

int main() {
    thread barberThread(runBarber);
    vector<thread> customers;
    for (int i = 0; i < 50; i++) {
        customers.push_back(thread(runCustomer));
        this_thread::sleep_for(chrono::microseconds(100));
    }
    for (int i = 0; i < 50; i++) customers[i].join();
    shop.close();
    barberThread.join();
    assert(shop.served + shop.turnedAway == 50);
    cout << "Solution 1 OK: served " << shop.served << ", turned away " << shop.turnedAway << "\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - queue of customer ids — BASICS §10

namespace advanced {
class BarberShop {
    std::mutex m;
    std::condition_variable customerArrived, haircutDone;
    std::queue<int> waiting;
    const size_t chairs;
    int lastServed = -1;
    bool closed = false;
public:
    std::atomic<int> served{0}, turnedAway{0};
    explicit BarberShop(size_t c) : chairs(c) {}

    bool getHaircut(int id) {                       // customer thread
        std::unique_lock lk(m);
        if (waiting.size() == chairs) { ++turnedAway; return false; }   // balk, don't block
        waiting.push(id);
        customerArrived.notify_one();               // wake the barber if asleep
        haircutDone.wait(lk, [&] { return lastServed >= id; });
        return true;
    }
    void barber() {
        for (;;) {
            std::unique_lock lk(m);
            customerArrived.wait(lk, [&] { return !waiting.empty() || closed; });   // sleep
            if (waiting.empty()) return;
            int id = waiting.front(); waiting.pop();
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::microseconds(300));          // cutting
            lk.lock();
            lastServed = id; ++served;
            haircutDone.notify_all();
        }
    }
    void close() { std::lock_guard lk(m); closed = true; customerArrived.notify_all(); }
};

void run() {
    BarberShop shop(3);
    std::thread barber(&BarberShop::barber, &shop);
    std::vector<std::thread> customers;
    for (int i = 0; i < 50; ++i) {
        customers.emplace_back(&BarberShop::getHaircut, &shop, i);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    for (auto& c : customers) c.join();
    shop.close(); barber.join();
    assert(shop.served + shop.turnedAway == 50);
    std::cout << "Solution 2 OK: served " << shop.served << ", turned away " << shop.turnedAway << "\n";
}
// Ids are increasing and served FIFO, so "lastServed >= id" tells a customer their cut is done.
// Follow-up: multiple barbers (Little Book of Semaphores 5.3, "Hilzer's barbershop").
}  // namespace advanced
