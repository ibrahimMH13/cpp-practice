#include <iostream>
#include <string>
#include <chrono>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>
#include <queue>
#include <vector>
#include <atomic>
using Clock = std::chrono::steady_clock;

// Task
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

// Handler
struct ITaskHandler
{
    virtual ~ITaskHandler() = default;
    virtual TaskResult handle(const Task &t) = 0;
};

//  queue
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
// RetryScheduler
class RetryScheduler
{
public:
    explicit RetryScheduler(std::function<bool(Task)> enqueueFn)
        : enqueueFn_(std::move(enqueueFn)), th_([this]
                                                { loop(); }) {}
    ~RetryScheduler()
    {
        stop();
    };
    void schedule(Task t, Clock::time_point due)
    {
        std::lock_guard<std::mutex> lock(m_);
        if (stopping_)
            return;
        heap_.push(Item{due, std::move(t)});
        cv_.notify_one();
    };
    void stop()
    {
        bool expected = false;
        if (!stopped_.compare_exchange_strong(expected, true))
            return;

        {
            std::lock_guard<std::mutex> lock(m_);
            stopping_ = true;
        }
        cv_.notify_one();
        if (th_.joinable())
            th_.join();
    };
    bool empty()
    {
        std::lock_guard lock(m_);
        return heap_.empty();
    };

private:
    struct Item
    {
        Clock::time_point due;
        Task task;
    };

    struct Cmp
    {
        bool operator()(const Item &a, Item &b)
        {
            return a.due > b.due;
        }
    };

    void loop()
    {
        std::unique_lock<std::mutex> lock(m_);
        while (true)
        {
            cv_.wait(lock, [&]
                     { return stopping_ || !heap_.empty(); });
            if (stopping_)
                break;
            while (!heap_.empty())
            {
                auto nextDue = heap_.top().due;
                cv_.wait_until(lock, nextDue, [&]
                               { return stopping_ || heap_.empty() || heap_.top().due != nextDue; });

                if (stopping_)
                    break;
                if (heap_.empty())
                    continue;

                if (Clock::now() < nextDue)
                {
                    continue;
                }
                Item it = heap_.top();
                heap_.pop();
                lock.unlock();
                (void)enqueueFn_(std::move(it.task));
                lock.lock();

                if (stopping_)
                    break;
                if (!heap_.empty() && heap_.top().due > Clock::now())
                {
                    break;
                }
                if (stopping_)
                    break;
            }
        }
    };

    std::function<bool(Task)> enqueueFn_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::priority_queue<Item, std::vector<Item>, Cmp> heap_;
    std::thread th_;
    bool stopping_ = false;
    std::atomic<bool> stopped_ = false;
};
// worker pool
class WorkerPool
{
public:
    enum class ShutdownMode
    {
        Drain,
        Cancel
    };
    WorkerPool(size_t workers, size_t queueCap, ITaskHandler &handler, int maxAttempts = 3, std::chrono::milliseconds baseBackOff = std::chrono::milliseconds(200)) : q_(queueCap), handler_(handler),
                                                                                                                                                                      retry_([this](Task t)
                                                                                                                                                                             { return q_.push(std::move(t)); }),
                                                                                                                                                                      max_attempts_(maxAttempts), baseBackOff_(baseBackOff)
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
            retry_.stop();
            q_.cancel();
        }
        else
        {
            std::unique_lock<std::mutex> lock(drainMtx_);
            drainCv_.wait(lock, [&]
                          { return q_.empty() && inFlight_.load(std::memory_order_acquire) == 0 && retry_.empty(); });
            retry_.stop();
            q_.cancel();
        }

        for (auto &th : threads_)
        {
            if (th.joinable())
                th.join();
        }
    };

private:
    static std::chrono::milliseconds backoff(int attempt, std::chrono::milliseconds base)
    {

        long long ms = base.count();
        for (int i = 0; i < attempt; i++)
        {
            ms *= 2;
            if (ms > 5000)
            {
                ms = 5000;
                break;
            }
        }
        return std::chrono::milliseconds(ms);
    }

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
                if (t.attempt < max_attempts_)
                {
                    auto delay = backoff(t.attempt, baseBackOff_);
                    retry_.schedule(std::move(t), Clock::now() + delay);
                }
            }
            inFlight_.fetch_sub(1, std::memory_order_acq_rel);
            if (inFlight_.load(
                    std::memory_order_acquire) == 0 &&
                q_.empty() &&
                retry_.empty())
            {
                std::lock_guard<std::mutex> lock(drainMtx_);
                drainCv_.notify_all();
            }
        }
    };
    BoundBlockQueue<Task> q_;
    ITaskHandler &handler_;
    RetryScheduler retry_;
    int max_attempts_;
    std::chrono::milliseconds baseBackOff_;
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
        std::cout << "TASK ID" << t.id << "SUCCESS";
        return TaskResult::SUCCESS;
    }
};

// ---------------- Fair, bounded, blocking queue ----------------
struct FairTask
{
    std::string id;
    std::string tenantId;
    std::string payload;
    int attempt{0};
};

class FairTaskQueue
{
public:
    explicit FairTaskQueue() {}
    bool push(FairTask t) {
        std::unique_lock<std::mutex> lock(m_);
        cv_not_full_.wait(lock,[&]{
            return canceled_ || closed_ || size_ < capacity_;
        });
        if (canceled_ || closed_)
        {
            return false;
        }
         auto& q = perTenant_[t.tenantId];
         bool wasEmpty = q.empty();
         q.push_back(std::move(t));
         size_++;

         if (wasEmpty)
         {
            activeRing_.push_back(t.tenantId);
         }
         cv_not_empty_.notify_one();
         return true;
    }
    bool pull(FairTask out)
    {
        std::unique_lock<std::mutex> lock(m_);
        cv_not_empty_.wait(lock, [&]
                           { return canceled_ || size_ > 0 || closed_; });
        if (canceled_)
        {
            return false;
        }
        if (size_ == 0)
        {
            return false;
        }
        //Round Robin
        std::string tenant = std::move(activeRing_.front());
        activeRing_.pop_front();
        auto it = perTenant_.find(tenant);

        if(it == perTenant_.end() || it->second.empty()){
            return pull(out);
        }
         auto& tq = it->second;
         out = std::move(tq.front());
         tq.pop_front();
         size_--;
         if (!tq.empty())
         {
            activeRing_.push_back(tenant);
         }else{
            perTenant_.erase(it);
         }
         cv_not_full_.notify_one();
         return true;
         
    }
    void cancel()
    {
        std::lock_guard<std::mutex> lock(m_);
        canceled_ = true;
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }
    void close()
    {
        std::lock_guard<std::mutex> lock(m_);
        closed_ = true;
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }
    bool empty() const
    {
        std::lock_guard<std::mutex> lock(m_);
        return size_ == 0;
    }

private:
    size_t capacity_;
    mutable std::mutex m_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    std::unordered_map<std::string, std::deque<FairTask>> perTenant_;
    std::deque<std::string> activeRing_;

    size_t size_ = 0;
    bool closed_ = false;
    bool canceled_ = false;
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