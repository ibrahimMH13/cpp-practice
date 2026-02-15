// fifo_scheduler.cpp
// C++17, STL only
// FIFO scheduler with the SAME style/API as your TaskScheduler:
//   submit(Task), cancel(task_id), tryGetNext(), getNext(), shutdown()
// Notes:
// - FIFO order by arrival (not by priority).
// - cancel() is "lazy": task stays in queue, skipped when popped (one-time cancel marker).
// - getNext() blocks until a task is available or shutdown() is called.

#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <iostream>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <chrono>

struct Task
{
    std::string task_id;
    int priority = 0;        // kept for consistency with your other schedulers; FIFO ignores it
    std::uint64_t ts = 0;    // kept for debugging / tracking; FIFO ignores it for ordering
};

class FifoTaskScheduler
{
public:
    // Submit task into FIFO queue. Returns false if scheduler is shutdown.
    bool submit(Task t)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (shutdown_) return false;

            // revive if previously canceled
            canceled_.erase(t.task_id);

            q_.push_back(std::move(t));
        }
        cv_.notify_one();
        return true;
    }

    // Lazy cancel: mark id; if it appears later, it will be skipped once and the marker removed.
    bool cancel(const std::string& taskId)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return canceled_.insert(taskId).second;
    }

    // Non-blocking.
    std::optional<Task> tryGetNext()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (shutdown_) return std::nullopt;
        return popOneUnlocked();
    }

    // Blocking.
    std::optional<Task> getNext()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        for (;;)
        {
            cv_.wait(lock, [&] { return shutdown_ || !q_.empty(); });
            if (shutdown_) return std::nullopt;

            if (auto t = popOneUnlocked())
                return t;

            // If we got here, it means queue had only canceled items and became empty.
            // Loop back to wait for new tasks or shutdown.
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
        return q_.empty();
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return q_.size();
    }

private:
    // Pop one FIFO task, skipping canceled ones (one-time marker).
    // Must be called with mtx_ held.
    std::optional<Task> popOneUnlocked()
    {
        while (!q_.empty())
        {
            Task t = std::move(q_.front());
            q_.pop_front();

            auto it = canceled_.find(t.task_id);
            if (it != canceled_.end())
            {
                canceled_.erase(it); // one-time cancel marker
                continue;            // skip this task
            }

            return t;
        }
        return std::nullopt;
    }

private:
    std::deque<Task> q_;
    std::unordered_set<std::string> canceled_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool shutdown_ = false;
};

int main()
{
    FifoTaskScheduler sched;

    const int workerCount = 3;
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (int i = 0; i < workerCount; ++i)
    {
        workers.emplace_back([&sched, i] {
            while (auto t = sched.getNext())
            {
                std::cout << "[Worker=" << i << "] "
                          << "task=" << t->task_id
                          << " priority=" << t->priority
                          << " ts=" << t->ts << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            std::cout << "[Worker=" << i << "] exiting\n";
        });
    }

    // Submit some tasks
    sched.submit({"a", 102, 24});
    sched.submit({"b", 102, 25});
    sched.submit({"c", 100, 26});
    sched.submit({"d", 101, 27});

    // Cancel one task lazily (it will be skipped once it reaches the head)
    sched.cancel("b");

    // Add more tasks later to observe concurrency
    for (int k = 0; k < 6; ++k)
    {
        sched.submit({"x" + std::to_string(k), 100 + (k % 3), 1000 + (std::uint64_t)k});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Demo-only: let workers process for a bit
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Stop workers cleanly (otherwise getNext blocks forever)
    sched.shutdown();

    for (auto& th : workers) th.join();
    return 0;
}
