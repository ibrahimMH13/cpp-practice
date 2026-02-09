#include <string>
#include <optional>
#include <queue>
#include <cstdint>
#include <vector>
#include <iostream>
#include <unordered_set>
#include <mutex>
#include <condition_variable>

struct Task
{
    std::string task_id;
    int priority = 0;
    std::uint64_t ts = 0;
};

struct TaskWorse
{
    bool operator()(Task const &a, Task const &b) const noexcept
    {
        if (a.priority != b.priority)
            return a.priority < b.priority;
        return a.ts > b.ts;
    }
};

class TaskScheduler
{
public:
    void submit(Task t)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            taskCanceledId_.erase(t.task_id);
            pq_.push(std::move(t));
        }
        cv_.notify_one();
    }

    bool cancel(std::string taskId)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return taskCanceledId_.insert(taskId).second;
    }

    std::optional<Task> tryGetNext()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        if (shutdown_)
            return std::nullopt;
        while (!pq_.empty())
        {
            Task best = pq_.top();
            pq_.pop();
            if (taskCanceledId_.find(best.task_id) != taskCanceledId_.end())
            {
                taskCanceledId_.erase(best.task_id);
                continue;
            }
            return best;
        }
        return std::nullopt;
    }

    std::optional<Task> getNext()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        if (shutdown_)
            return std::nullopt;

        for (;;)
        {
            cv_.wait(lock, [&]
                     { return shutdown_ || !pq_.empty(); });
            if (shutdown_)
                return std::nullopt;

            while (!pq_.empty())
            {
                Task best = pq_.top();
                pq_.pop();
                if (taskCanceledId_.find(best.task_id) != taskCanceledId_.end())
                {
                    taskCanceledId_.erase(best.task_id);
                    continue;
                }
                return best;
            }
        }
    }

    bool empty() const noexcept
    {
        std::unique_lock<std::mutex> lock(mtx_);
        return pq_.empty();
    }
    std::size_t size() const noexcept { return pq_.size(); }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            shutdown_ = true;
        }
        cv_.notify_all();
    }

private:
    std::priority_queue<Task, std::vector<Task>, TaskWorse> pq_;
    std::unordered_set<std::string> taskCanceledId_;
    mutable std::mutex mtx_;
    bool shutdown_ = false;
    std::condition_variable cv_;
};

int main()
{

    TaskScheduler taskScheduler;
    taskScheduler.submit({"a",
                          102,
                          24});
    taskScheduler.submit({"b",
                          102,
                          24});
    taskScheduler.submit({"c",
                          100,
                          242});
    taskScheduler.submit({"d",
                          101,
                          241});

    taskScheduler.cancel("b");

  // taskScheduler.tryGetNext();
    while (auto t = taskScheduler.getNext())
    {
        std::cout << "==> task id: " << t->task_id
                  << " priority: " << t->priority
                  << " ts: " << t->ts
                  << "\n";
    }

    return 0;
}
