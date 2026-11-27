// Multithreaded Web Crawler (LeetCode 1242)
// From a start URL, crawl every reachable URL on the same hostname, using several threads.
// Each URL fetched once. getUrls() is slow (simulated), so parallelism matters.
// Core difficulty: knowing when you're DONE — the queue being empty isn't enough, because
// a busy worker may be about to add more. Track in-flight work: done = queue empty && active == 0.
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cassert>
using namespace std;

// =====================================================================================
// SOLUTION 1 — beginner-friendly. Read this first.
// Plain class, while(...) cv.wait(lock), simple names. This is what to write in an interview.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - unordered_set to remember visited URLs; .count(x) is 1 if present — BASICS §10
//  - the termination rule: stop only when nothing is queued AND no worker is busy

namespace advanced { void run(); }

// The fake internet: page -> links on that page. getUrls is slow, like a real HTTP fetch.
unordered_map<string, vector<string>> fakeWeb;
vector<string> getUrls(string url) {
    this_thread::sleep_for(chrono::milliseconds(15));
    if (fakeWeb.count(url)) return fakeWeb[url];
    return {};
}

string hostnameOf(string url) {                 // "http://a.com/x" -> "a.com"
    int start = url.find("//") + 2;
    int end = url.find('/', start);
    return url.substr(start, end - start);
}

class Crawler {
    mutex m;
    condition_variable cv;
    queue<string> toVisit;                      // URLs found but not fetched yet
    unordered_set<string> seen;                 // every URL ever queued (so nothing is fetched twice)
    int busyWorkers;                            // workers currently fetching a page
    string hostname;

    void worker() {
        while (true) {
            string url;
            {
                unique_lock<mutex> lock(m);
                // Wait while there's nothing to do BUT someone is still fetching (they may add URLs).
                while (toVisit.empty() && busyWorkers > 0) cv.wait(lock);
                if (toVisit.empty()) {          // nothing queued and nobody busy: crawl is finished
                    cv.notify_all();
                    return;
                }
                url = toVisit.front();
                toVisit.pop();
                busyWorkers++;
            }

            vector<string> links = getUrls(url);    // slow part, done WITHOUT the lock

            {
                lock_guard<mutex> lock(m);
                for (string link : links) {
                    // seen.count(link) == 0 means "never seen this URL before" (BASICS §10)
                    if (hostnameOf(link) == hostname && seen.count(link) == 0) {
                        seen.insert(link);
                        toVisit.push(link);
                    }
                }
                busyWorkers--;
            }
            cv.notify_all();
        }
    }

public:
    vector<string> crawl(string startUrl, int numWorkers) {
        hostname = hostnameOf(startUrl);
        seen.insert(startUrl);
        toVisit.push(startUrl);
        busyWorkers = 0;

        vector<thread> workers;
        // thread(&Crawler::worker, this): run this object's worker() on a new thread (BASICS §8)
        for (int i = 0; i < numWorkers; i++) workers.push_back(thread(&Crawler::worker, this));
        for (int i = 0; i < numWorkers; i++) workers[i].join();

        vector<string> result(seen.begin(), seen.end());
        return result;
    }
};

// ---------- test ----------
int main() {
    for (int i = 0; i < 40; i++) {
        string page = "http://news.site/" + to_string(i) + "/";
        fakeWeb[page] = { "http://news.site/" + to_string((i * 3 + 1) % 40) + "/",
                          "http://news.site/" + to_string((i * 7 + 2) % 40) + "/",
                          "http://other.site/" + to_string(i) + "/" };     // different host: skip
    }
    Crawler crawler;
    auto start = chrono::steady_clock::now();
    vector<string> pages = crawler.crawl("http://news.site/0/", 8);
    auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();
    for (string p : pages) assert(hostnameOf(p) == "news.site");
    cout << "Crawled " << pages.size() << " pages in " << ms << " ms\nSolution 1 OK\n";
    advanced::run();
}

// =====================================================================================
// SOLUTION 2 — advanced. Same ideas with more idiomatic C++ (RAII helpers, lambdas,
// templates, std::optional) plus extra variants and follow-ups. Read after Solution 1.
// =====================================================================================

// NEW IN THIS FILE (explained in BASICS.md):
//  - lambdas — BASICS §13

namespace advanced {
struct HtmlParser {                                 // fake web
    std::unordered_map<std::string, std::vector<std::string>> links;
    std::vector<std::string> getUrls(const std::string& u) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        auto it = links.find(u);
        return it == links.end() ? std::vector<std::string>{} : it->second;
    }
};

std::string host(const std::string& url) {          // "http://a.com/x" -> "a.com"
    auto start = url.find("//") + 2;
    return url.substr(start, url.find('/', start) - start);
}

std::vector<std::string> crawl(const std::string& start, HtmlParser& parser, int workers = 8) {
    const std::string h = host(start);
    std::mutex m; std::condition_variable cv;
    std::queue<std::string> frontier; std::unordered_set<std::string> seen{start};
    int active = 0;
    frontier.push(start);

    auto worker = [&] {
        for (;;) {
            std::string url;
            {
                std::unique_lock lk(m);
                cv.wait(lk, [&] { return !frontier.empty() || active == 0; });
                if (frontier.empty()) return;            // nothing queued and nobody working: done
                url = frontier.front(); frontier.pop(); ++active;
            }
            auto next = parser.getUrls(url);             // slow I/O outside the lock
            {
                std::lock_guard lk(m);
                for (auto& n : next)
                    if (host(n) == h && seen.insert(n).second) frontier.push(n);
                --active;
            }
            cv.notify_all();
        }
    };
    std::vector<std::thread> ts;
    for (int i = 0; i < workers; ++i) ts.emplace_back(worker);
    for (auto& t : ts) t.join();
    return {seen.begin(), seen.end()};
}

void run() {
    HtmlParser p;
    for (int i = 0; i < 40; ++i) {
        std::string u = "http://news.site/" + std::to_string(i) + "/";
        p.links[u] = {"http://news.site/" + std::to_string((i * 3 + 1) % 40) + "/",
                      "http://news.site/" + std::to_string((i * 7 + 2) % 40) + "/",
                      "http://other.site/" + std::to_string(i) + "/"};
    }
    auto t0 = std::chrono::steady_clock::now();
    auto res = crawl("http://news.site/0/", p);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    for (auto& u : res) assert(host(u) == "news.site");
    std::cout << "Crawled " << res.size() << " pages in " << ms << " ms (sequential would be ~" << res.size() * 15 << " ms)\nSolution 2 OK\n";
}
}  // namespace advanced
