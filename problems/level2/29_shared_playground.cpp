// Shared playground access.
// Several teams share one playground. Only ONE team may be on it at a time, at most 10 players at
// once, and no team may starve (wait forever while other teams keep playing).
//
// This is the party-bathroom problem (problem 11) generalised from 2 parties and capacity 3 to any
// number of teams and capacity 10, and the starvation requirement is now explicit.
//
// KEY LEARNING POINTS
//  - Three rules to check before letting a player in: is it their team's turn, is there room, and
//    would letting them in starve a team that's waiting?
//  - Starvation happens when the team already playing keeps admitting new players, so the playground
//    never empties and the waiting team never gets a turn.
//  - Fix 1 (Solution 1): strict first-come-first-served with tickets, like a bank queue. Nobody can
//    overtake, so nobody starves. Cost: a slot can stay empty while the next person in line belongs
//    to another team.
//  - Fix 2 (Solution 2): "turns". Teammates may keep joining, but once another team is waiting, the
//    current team may admit at most K more players; then the playground drains and the team that has
//    waited longest goes next. Better use of the 10 slots, and every wait is still bounded.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <vector>
#include <deque>
#include <map>
#include <algorithm>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - the ticket system from problem 11 (nextTicket / nowServing), unchanged — BASICS §5
//  - a "starvation test": one team floods the playground while another team waits; if the code
//    could starve, the test would hang instead of finishing

namespace advanced { void run(); }

class Playground {
    mutex m;
    condition_variable cv;
    const int CAPACITY = 10;
    int nextTicket = 0;            // given to each arriving player
    int nowServing = 0;            // the ticket allowed to try entering now
    int inside = 0;
    int teamInside = -1;           // only meaningful when inside > 0

public:
    void enter(int team) {
        unique_lock<mutex> lock(m);
        int myTicket = nextTicket++;
        // Enter when: it's my turn in line, there's room, and the playground is empty or has my team.
        while (myTicket != nowServing || inside == CAPACITY || (inside > 0 && teamInside != team))
            cv.wait(lock);
        inside++;
        teamInside = team;
        nowServing++;              // the next player in line may also be my teammate
        cv.notify_all();
    }

    void leave() {
        lock_guard<mutex> lock(m);
        inside--;
        cv.notify_all();
    }
};

// ---------- test ----------
// Checks run while players are inside: never two teams, never more than 10 players.
atomic<int> playersOnTeam[3];

void play(int team) {
    int now = ++playersOnTeam[team];
    assert(now <= 10);
    for (int other = 0; other < 3; other++) if (other != team) assert(playersOnTeam[other] == 0);
    this_thread::sleep_for(chrono::microseconds(200));
    playersOnTeam[team]--;
}

Playground* field;
atomic<bool> team1Finished(false);

void floodingPlayer() {                    // team 0 keeps coming back until team 1 is done
    while (!team1Finished) { field->enter(0); play(0); field->leave(); }
}
void visitingPlayer(int team) {
    for (int i = 0; i < 20; i++) { field->enter(team); play(team); field->leave(); }
}

int main() {
    field = new Playground();
    vector<thread> flood, visitors;
    for (int i = 0; i < 12; i++) flood.push_back(thread(floodingPlayer));     // more than fit at once
    for (int i = 0; i < 3; i++) visitors.push_back(thread(visitingPlayer, 1));
    for (int i = 0; i < 3; i++) visitors.push_back(thread(visitingPlayer, 2));
    for (int i = 0; i < 6; i++) visitors[i].join();    // would hang here if teams 1 or 2 could starve
    team1Finished = true;
    for (int i = 0; i < 12; i++) flood[i].join();
    delete field;
    cout << "Solution 1 OK: teams 1 and 2 got their turns while team 0 flooded the playground\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - "turns" instead of strict FIFO: teammates may overtake each other, but only K times once
//    another team is waiting
//  - a queue of TEAMS (not players) ordered by who has waited longest; a team that just played
//    goes to the back
//  - a small metric (how full the playground was when each player entered), to compare policies

namespace advanced {

class TurnPlayground {
    std::mutex m;
    std::condition_variable cv;
    const int capacity, maxBypass;
    int inside = 0, current = -1, admittedThisTurn = 0;
    std::map<int, int> waiting;               // team -> players waiting
    std::deque<int> teamOrder;                // teams with waiting players, longest-waiting first

    bool otherTeamWaiting() const {
        for (auto& [team, n] : waiting) if (team != current && n > 0) return true;
        return false;
    }
    bool canEnter(int team) const {
        if (inside == 0) return !teamOrder.empty() && teamOrder.front() == team;   // empty: whose turn?
        return team == current && inside < capacity &&
               (!otherTeamWaiting() || admittedThisTurn < maxBypass);           // bounded overtaking
    }
    void moveToBack(int team) {
        teamOrder.erase(std::remove(teamOrder.begin(), teamOrder.end(), team), teamOrder.end());
        if (waiting[team] > 0) teamOrder.push_back(team);
    }

public:
    std::atomic<long> admitted{0}, fullnessSum{0};   // test metrics: average players inside at entry

    TurnPlayground(int capacity, int maxBypass) : capacity(capacity), maxBypass(maxBypass) {}

    void enter(int team) {
        std::unique_lock lk(m);
        if (waiting[team]++ == 0) teamOrder.push_back(team);
        cv.wait(lk, [&] { return canEnter(team); });
        --waiting[team];
        if (inside == 0 && team != current) admittedThisTurn = 0;   // a new turn starts
        current = team;
        ++inside;
        ++admittedThisTurn;
        moveToBack(team);                    // after playing, this team queues behind the others
        ++admitted; fullnessSum += inside;
        cv.notify_all();
    }
    void leave() {
        { std::lock_guard lk(m); --inside; }
        cv.notify_all();
    }
};

void run() {
    TurnPlayground p(10, 20);
    std::atomic<bool> done{false};
    for (auto& c : playersOnTeam) c = 0;
    std::vector<std::thread> flood, visitors;
    for (int i = 0; i < 12; ++i) flood.emplace_back([&] { while (!done) { p.enter(0); play(0); p.leave(); } });
    for (int t = 1; t <= 2; ++t)
        for (int i = 0; i < 3; ++i)
            visitors.emplace_back([&, t] { for (int k = 0; k < 20; ++k) { p.enter(t); play(t); p.leave(); } });
    for (auto& v : visitors) v.join();
    done = true;
    for (auto& f : flood) f.join();
    std::printf("Solution 2 OK: no starvation; average %.1f players already inside when someone entered\n",
                (double)p.fullnessSum / p.admitted);
}
}  // namespace advanced

// Interview follow-ups to raise yourself:
//  - Choosing K: K = 0 means strict alternation between teams once anyone is waiting (fair, but the
//    field is often half empty); a large K uses the field well but makes waiting teams wait longer.
//    The worst-case wait is about (number of teams) x (K + 10) play sessions.
//  - Priority teams, or a time limit per turn instead of a count: same structure, different canEnter.
//  - Many teams: keep teamOrder as a queue and waiting counts in a map, as here, so checks stay cheap.
//  - This is the same family as readers-writers (problem 9): a "team" is like "readers", and turns
//    are the fairness fix that stops one side starving the other.
