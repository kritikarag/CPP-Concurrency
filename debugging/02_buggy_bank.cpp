// DEBUGGING DRILL 2 — 3 bugs. Timebox: 20 minutes.
// Threads transfer money between accounts at random. Total money must never change.
// Symptoms: the program occasionally freezes; the audit sometimes reports a wrong total.
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <random>

struct Account { std::mutex m; long balance = 1000; };

class Bank {
public:
    std::vector<Account> accounts;
    explicit Bank(int n) : accounts(n) {}

    void transfer(int from, int to, long amount) {
        std::lock_guard a(accounts[from].m);
        std::lock_guard b(accounts[to].m);
        if (accounts[from].balance < amount) return;
        accounts[from].balance -= amount;
        accounts[to].balance += amount;
    }
    long audit() {
        long total = 0;
        for (auto& acc : accounts) total += acc.balance;
        return total;
    }
};

int main() {
    Bank bank(10);
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) ts.emplace_back([&, t] {
        std::mt19937 rng(t);
        for (int i = 0; i < 100000; ++i) {
            int a = rng() % 10, b = rng() % 10;
            bank.transfer(a, b, rng() % 100);
        }
    });
    std::thread auditor([&] { for (int i = 0; i < 1000; ++i) if (bank.audit() != 10000) std::cout << "AUDIT FAILED\n"; });
    for (auto& t : ts) t.join();
    auditor.join();
    std::cout << "final total = " << bank.audit() << "\n";
}
