// fair_scheduler.cpp
// C++17
// Per-tenant Round-Robin scheduler
// Same API as fifo_scheduler.cpp

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
    int priority = 0; // not used for fairness here
    std::uint64_t ts = 0;
};

class FairTaskScheduler
{
public:
    bool submit(Task t)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (shutdown_)
                return false;

            canceled_.erase(t.task_id);

            auto &tenantQueue = perTenant_[t.tenant_id];
            bool wasEmpty = tenantQueue.empty();
            tenantQueue.push_back(std::move(t));

            if (wasEmpty)
                activeRing_.push_back(t.tenant_id);
        }
        cv_.notify_one();
        return true;
    }

    bool cancel(const std::string &taskId)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return canceled_.insert(taskId).second;
    }

    std::optional<Task> tryGetNext()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (shutdown_)
            return std::nullopt;
        return popOneUnlocked();
    }

    std::optional<Task> getNext()
    {
        std::unique_lock<std::mutex> lock(mtx_);

        for (;;)
        {
            cv_.wait(lock, [&]
                     { return shutdown_ || !activeRing_.empty(); });
            if (shutdown_)
                return std::nullopt;

            if (auto t = popOneUnlocked())
                return t;
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
        return activeRing_.empty();
    }

private:
    std::optional<Task> popOneUnlocked()
    {
        while (!activeRing_.empty())
        {
            std::string tenant = std::move(activeRing_.front());
            activeRing_.pop_front();

            auto it = perTenant_.find(tenant);
            if (it == perTenant_.end() || it->second.empty())
                continue;

            auto &tenantQueue = it->second;

            while (!tenantQueue.empty())
            {
                Task t = std::move(tenantQueue.front());
                tenantQueue.pop_front();

                auto cancelIt = canceled_.find(t.task_id);
                if (cancelIt != canceled_.end())
                {
                    canceled_.erase(cancelIt);
                    continue;
                }

                if (!tenantQueue.empty())
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
    std::unordered_set<std::string> canceled_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool shutdown_ = false;
};

int main()
{
    FairTaskScheduler sched;

    const int workerCount = 3;
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (int i = 0; i < workerCount; ++i)
    {
        workers.emplace_back([&sched, i]
                             {
            while (auto t = sched.getNext())
            {
                std::cout << "[Worker=" << i << "] "
                          << "tenant=" << t->tenant_id
                          << " task=" << t->task_id
                          << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            std::cout << "[Worker=" << i << "] exiting\n"; });
    }

    // Tenant A floods
    for (int i = 0; i < 10; ++i)
        sched.submit({"A" + std::to_string(i), "A", 0, static_cast<std::uint64_t>(i)});

    // Tenant B smaller
    for (int i = 0; i < 3; ++i)
        sched.submit({"B" + std::to_string(i), "B", 0, static_cast<std::uint64_t>(i)});

    // Tenant C smaller
    for (int i = 0; i < 3; ++i)
        sched.submit({"C" + std::to_string(i), "C", 0, static_cast<std::uint64_t>(i)});

    sched.cancel("A5");

    std::this_thread::sleep_for(std::chrono::seconds(2));
    sched.shutdown();

    for (auto &th : workers)
        th.join();

    return 0;
}
