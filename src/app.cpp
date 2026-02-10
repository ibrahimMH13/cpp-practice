#include <iostream>
#include <string>
#include <chrono>
#include <deque>
#include <condition_variable>
#include <mutex>


struct Task
{
    std::string id;
    std::string payload;
    int attempt {0};
    std::chrono::steady_clock::time_point createdAt{std::chrono::steady_clock::now()};
};

enum TaskResult{
    SUCCESS,
    RETRYABLE_FAIL,
    PERMANENT_FAIL
};

struct ITaskHandler
{
    virtual ~ITaskHandler() = default;
    virtual TaskResult handler(const Task& t) =0;
};


template<typename T>
class BoundBlockQueue
{
    public:
        explicit BoundBlockQueue(size_t capacity):capacity_(capacity){}
        
        bool push(T item){

            std::unique_lock<std::mutex> lock(mtx_);
            cv_not_full_.wait(lock,[&]{
                return closed_ || dq_.size() < capacity_;
            });
            if(closed_) return false;
            dq_.push_back(std::move(item));
            cv_not_empty_.notify_one();
            return true;
        }

        bool pull(T& out){
            std::unique_lock<std::mutex> lock(mtx_);
            cv_not_empty_.wait(lock,[&]{
                return closed_ || !dq_.empty();
            });
            if (dq_.empty()) return false;
            out = std::move(dq_.front());
            dq_.pop_front();
            cv_not_full_.notify_one();
            return true;
            
        }

        void close(){
            std::lock_guard<std::mutex> lock(mtx_);
            closed_ = true;
            cv_not_empty_.notify_all();
            cv_not_full_.notify_all();
        }
    
    private:
    std::size_t capacity_;
    bool closed_ =  false;
    std::deque<T> dq_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    std::mutex mtx_;
};





int main(){

    std::cout << "Hello!!!\n";

    return 0;
}