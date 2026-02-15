// priority_scheduler.cpp
// C++17, STL only
//
// Priority scheduler with starvation protection via budgets (weighted service).
// SAME style/API as fifo_scheduler.cpp and fair_scheduler.cpp:
//
//   submit(Task)
//   cancel(task_id)
//   tryGetNext()
//   getNext()
//   shutdown()
//
// Design:
// - 3 priority bands: P0 (highest), P1, P2 (lowest).
// - Within each band, we schedule FAIR by tenant (round-robin) using the same Fair logic.
// - Across bands, we schedule using budgets per cycle:
//      budgets = { p0=70, p1=30, p2=1 }  (example)
//   This prevents starvation: even if P0 is always busy, P1/P2 still get serviced.
//
// Notes:
// - cancel() is lazy (one-time cancel marker).
// - getNext() blocks until any band has work or shutdown() is called.
// - For simplicity, we keep one condition_variable for "any work arrived".
//   This is interview-grade and easy to reason about.

#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <chrono>

struct Task
{
    std::string task_id;
    std::string tenant_id;
    int priorityBand = 0;     // 0=P0, 1=P1, 2=P2
    std::uint64_t ts = 0;
};

// --------- Fair-by-tenant queue core (internal) ---------
class FairBandQueue
{
public:
    void push(Task t)
    {
        auto& tq = perTenant_[t.tenant_id];
        bool wasEmpty = tq.empty();
        tq.push_back(std::move(t));
        if (wasEmpty)
            activeRing_.push_back(t.tenant_id);
    }

    bool empty() const noexcept
    {
        return activeRing_.empty();
    }

    // Pop one task fairly by tenant. Returns nullopt if empty.
    std::optional<Task> popOne(std::unordered_set<std::string>& canceled)
    {
        while (!activeRing_.empty())
        {
            std::string tenant = std::move(activeRing_.front());
            activeRing_.pop_front();

            auto it = perTenant_.find(tenant);
            if (it == perTenant_.end() || it->second.empty())
                continue;

            auto& tq = it->second;

            while (!tq.empty())
            {
                Task t = std::move(tq.front());
                tq.pop_front();

                auto cit = canceled.find(t.task_id);
                if (cit != canceled.end())
                {
                    canceled.erase(cit); // one-time marker
                    continue;
                }

                if (!tq.empty())
                    activeRing_.push_back(tenant);
                else
                    perTenant_.erase(it);

                return t;
            }

            perTenant_.erase(it);
        }
        return std::nullopt;
    }

private:
    std::unordered_map<std::string, std::deque<Task>> perTenant_;
    std::deque<std::string> activeRing_;
};

// --------- Budgeted Priority Scheduler ---------
struct Budgets
{
    int p0 = 70;
    int p1 = 30;
    int p2 = 1;
};

class PriorityTaskScheduler
{
public:
    explicit PriorityTaskScheduler(Budgets b = Budgets{}) : budgets_(b) {}

    bool submit(Task t)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (shutdown_) return false;

            // revive if previously canceled
            canceled_.erase(t.task_id);

            int band = normalizeBand(t.priorityBand);
            t.priorityBand = band;

            if (band == 0) q0_.push(std::move(t));
            else if (band == 1) q1_.push(std::move(t));
            else q2_.push(std::move(t));

            // wake any waiter
            cv_.notify_one();
        }
        return true;
    }

    bool cancel(const std::string& taskId)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return canceled_.insert(taskId).second;
    }

    std::optional<Task> tryGetNext()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (shutdown_) return std::nullopt;
        return popByBudgetUnlocked();
    }

    std::optional<Task> getNext()
    {
        std::unique_lock<std::mutex> lock(mtx_);

        for (;;)
        {
            // Wait until shutdown OR any band has something
            cv_.wait(lock, [&] { return shutdown_ || hasAnyWorkUnlocked(); });
            if (shutdown_) return std::nullopt;

            if (auto t = popByBudgetUnlocked())
                return t;

            // If we got here, it means we woke up but only canceled items were present.
            // Loop again and wait.
        }
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            shutdown_ = true;
        }
        cv_.notify_all();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return !hasAnyWorkUnlocked();
    }

private:
    static int normalizeBand(int b)
    {
        if (b <= 0) return 0;
        if (b == 1) return 1;
        return 2;
    }

    bool hasAnyWorkUnlocked() const
    {
        return !q0_.empty() || !q1_.empty() || !q2_.empty();
    }

    void resetCycleUnlocked()
    {
        used0_ = used1_ = used2_ = 0;
    }

    // The core: budgeted selection across priority bands.
    // Within a band: fair by tenant.
    std::optional<Task> popByBudgetUnlocked()
    {
        // If all budgets consumed, reset cycle
        if (used0_ >= budgets_.p0 && used1_ >= budgets_.p1 && used2_ >= budgets_.p2)
            resetCycleUnlocked();

        // Try P0 then P1 then P2, but only if budget allows
        if (budgets_.p0 > 0 && used0_ < budgets_.p0)
        {
            if (auto t = q0_.popOne(canceled_)) { ++used0_; return t; }
        }
        if (budgets_.p1 > 0 && used1_ < budgets_.p1)
        {
            if (auto t = q1_.popOne(canceled_)) { ++used1_; return t; }
        }
        if (budgets_.p2 > 0 && used2_ < budgets_.p2)
        {
            if (auto t = q2_.popOne(canceled_)) { ++used2_; return t; }
        }

        // If budgets block us but there is still work in some band, we can reset and retry once.
        // This prevents "dead budget" when a band is empty but its budget isn't consumed.
        if (hasAnyWorkUnlocked())
        {
            resetCycleUnlocked();

            if (budgets_.p0 > 0)
                if (auto t = q0_.popOne(canceled_)) { ++used0_; return t; }
            if (budgets_.p1 > 0)
                if (auto t = q1_.popOne(canceled_)) { ++used1_; return t; }
            if (budgets_.p2 > 0)
                if (auto t = q2_.popOne(canceled_)) { ++used2_; return t; }
        }

        return std::nullopt;
    }

private:
    // Three fair-by-tenant bands
    FairBandQueue q0_, q1_, q2_;

    Budgets budgets_;
    int used0_{0}, used1_{0}, used2_{0};

    // Lazy cancel markers
    std::unordered_set<std::string> canceled_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool shutdown_{false};
};

int main()
{
    // Example: 70% P0, 30% P1, 1 slot for P2 each cycle
    PriorityTaskScheduler sched(Budgets{70, 30, 1});

    const int workerCount = 3;
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (int i = 0; i < workerCount; ++i)
    {
        workers.emplace_back([&sched, i] {
            while (auto t = sched.getNext())
            {
                std::cout << "[Worker=" << i << "] "
                          << "P" << t->priorityBand
                          << " tenant=" << t->tenant_id
                          << " task=" << t->task_id
                          << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::cout << "[Worker=" << i << "] exiting\n";
        });
    }

    // Flood P0 from tenant A
    for (int i = 0; i < 200; ++i)
        sched.submit({"P0-A-" + std::to_string(i), "A", 0, (std::uint64_t)i});

    // Some P1 from tenant B
    for (int i = 0; i < 40; ++i)
        sched.submit({"P1-B-" + std::to_string(i), "B", 1, (std::uint64_t)i});

    // Some P2 from tenant C
    for (int i = 0; i < 10; ++i)
        sched.submit({"P2-C-" + std::to_string(i), "C", 2, (std::uint64_t)i});

    // Cancel one task (lazy)
    sched.cancel("P1-B-5");

    // Demo-only: let workers run
    std::this_thread::sleep_for(std::chrono::seconds(2));

    sched.shutdown();

    for (auto& th : workers)
        th.join();

    return 0;
}
