#include <iostream>
#include <string>
#include <chrono>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <thread>

struct Task
{
    std::string id;
    std::string payload;
    int attempt{0};
    std::chrono::steady_clock::time_point createdAt{std::chrono::steady_clock::now()};
};

enum TaskResult
{
    SUCCESS,
    RETRYABLE_FAIL,
    PERMANENT_FAIL
};

struct ITaskHandler
{
    virtual ~ITaskHandler() = default;
    virtual TaskResult handle(const Task &t) = 0;
};

template <typename T>
class BoundBlockQueue
{
public:
    explicit BoundBlockQueue(size_t capacity) : capacity_(capacity) {}

    bool push(T item)
    {

        std::unique_lock<std::mutex> lock(mtx_);
        cv_not_full_.wait(lock, [&]
                          { return closed_ || canceled_ || dq_.size() < capacity_; });
        if (closed_ || canceled_)
            return false;
        dq_.push_back(std::move(item));
        cv_not_empty_.notify_one();
        return true;
    }

    bool pull(T &out)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_not_empty_.wait(lock, [&]
                           { return canceled_ || closed_ || !dq_.empty(); });
        if (canceled_)
            return false;
        if (dq_.empty())
            return false;

        out = std::move(dq_.front());
        dq_.pop_front();
        cv_not_full_.notify_one();
        return true;
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        closed_ = true;
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

    void cancel()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        canceled_ = true;
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return dq_.empty();
    }

private:
    std::size_t capacity_;
    bool closed_ = false;
    bool canceled_ = false;
    std::deque<T> dq_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    mutable std::mutex mtx_;
};

class WorkerPool
{
public:
    enum class ShutdownMode
    {
        Drain,
        Cancel
    };
    WorkerPool(size_t workers, size_t queueCap, ITaskHandler &handler, int maxAttempts = 3) : q_(queueCap), handler_(handler), max_attempts_(maxAttempts)
    {
        for (size_t i = 0; i < workers; i++)
        {
            threads_.emplace_back([this]
                                  { this->workerLoop(); });
        }
    };
    bool submit(Task t)
    {
        if (!accepting_.load(std::memory_order_relaxed))
            return false;
        return q_.push(std::move(t));
    };

    void stopAccepting()
    {
        accepting_.store(false, std::memory_order_relaxed);
    }

    void shutdown(ShutdownMode mode)
    {
        stopAccepting();
        if (mode == ShutdownMode::Cancel)
        {
            q_.cancel();
        }
        else
        {
            q_.close();
            std::unique_lock<std::mutex> lock(drainMtx_);
            drainCv_.wait(lock, [&]
                          { return q_.empty() && inFlight_.load(std::memory_order_acquire) == 0; });
            q_.cancel();
        }

        for (auto &th : threads_)
        {
            if (th.joinable())
                th.join();
        }
    };

private:
    void workerLoop()
    {
        Task t;
        while (q_.pull(t))
        {
            inFlight_.fetch_add(1, std::memory_order_acq_rel);
            TaskResult r = handler_.handle(t);
            if (r == TaskResult::RETRYABLE_FAIL)
            {
                t.attempt++;
                if (t.attempt >= max_attempts_)
                {
                    (void)q_.push(std::move(t));
                }
            }
            inFlight_.fetch_sub(1, std::memory_order_acq_rel);
            if(inFlight_.load(std::memory_order_acquire) == 0 && q_.empty()){
                std::lock_guard<std::mutex> lock(drainMtx_);
                drainCv_.notify_all();
            }
        }
    };
    BoundBlockQueue<Task> q_;
    ITaskHandler &handler_;
    int max_attempts_;
    std::vector<std::thread> threads_;
    std::atomic<bool> accepting_;
    std::atomic<int> inFlight_{0};
    std::mutex drainMtx_;
    std::condition_variable drainCv_;
};

// Example Handler
struct DemoHandler : ITaskHandler
{
    TaskResult handle(const Task &t) override
    {
        std::this_thread::sleep_for(std::chrono::microseconds(30));
        if (t.payload == "fail" && t.attempt < 2)
            return TaskResult::RETRYABLE_FAIL;
        return TaskResult::SUCCESS;
    }
};

int main()
{

    DemoHandler hdlr;

    WorkerPool pool(4, 64, hdlr);

    pool.submit(Task{"1", "ok"});
    pool.submit(Task{"2", "fail"});
    pool.submit(Task{"2", "fail"});
    pool.submit(Task{"1", "ok"});
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    pool.shutdown(WorkerPool::ShutdownMode::Drain);
    return 0;
}