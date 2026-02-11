#include <string>
#include <optional>
#include <queue>
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
//FIFO BoundedQueue
template <typename T>
class BoundedQueue
{

public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {};
    bool push(T item)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_not_full_.wait(lock, [&]
                          { return shutdown_ || q_.size() < capacity_; });
        if (shutdown_)
            return false;
        q_.push_back(std::move(item));
        cv_not_empty_.notify_one();
        return true;
    }
    std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_not_empty_.wait(lock, [&]
                           { return shutdown_ || !q_.empty(); });

        if (q_.empty())
        {
            return std::nullopt;
        }

        T item = std::move(q_.front());
        q_.pop_front();
        cv_not_full_.notify_one();
        return item;
    }
    void shutdown()
    {
        {
            std::unique_lock<std::mutex> lock(mtx_);
            shutdown_ = true;
        }
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

private:
    std::size_t capacity_;
    std::mutex mtx_;
    bool shutdown_ = false;
    std::condition_variable cv_not_full_;
    std::condition_variable cv_not_empty_;
    std::deque<T> q_;
};


int main()
{

    TaskScheduler taskScheduler;
    const int workerCount = 3;
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

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
    for (int i = 0; i < workerCount; ++i)
    {
        workers.emplace_back([&taskScheduler, i]
                             {
                                 while (auto t = taskScheduler.getNext())
                                 {
                                     std::cout << "[Worker=" << i << "] "
                                               << "task=" << t->task_id << " "
                                               << "priority=" << t->priority << " "
                                               << "ts=" << t->ts << " \n";
                                     std::cout << "[worker " << i << "] exiting\n";
                                 }
                             });
    }
    taskScheduler.submit({"a", 102, 24});
    taskScheduler.submit({"b", 102, 24});
    taskScheduler.submit({"c", 100, 242});
    taskScheduler.submit({"d", 101, 241});
    taskScheduler.cancel("b");
    // Add more tasks later to see concurrency
    for (int k = 0; k < 6; ++k)
    {
        taskScheduler.submit({"x" + std::to_string(k), 100 + (k % 3), 10 + (std::uint64_t)k});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Give workers time to drain queue (for demo only)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    //    // Stop workers and join
    //  taskScheduler.shutdown();
    for (auto &th : workers)
        th.join();

    return 0;
}
