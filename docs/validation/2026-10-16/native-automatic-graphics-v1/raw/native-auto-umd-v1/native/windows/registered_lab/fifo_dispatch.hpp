#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <vector>
namespace nr::registered_lab {
// Bounded FIFO ownership of one synchronous provider stream. Reserving a turn
// does not hold the mutex while another client is using the provider.
class FifoDispatch {
public:
    using Clock=std::chrono::steady_clock;
    struct Exhausted : std::runtime_error { Exhausted():std::runtime_error("render request budget exhausted"){} };
private:
    std::mutex mutex;
    std::condition_variable changed;
    uint32_t limit,issued=0,serving=0;
    bool cancelled=false;
    std::vector<bool> claimed;
public:
    explicit FifoDispatch(uint32_t maximum):limit(maximum),claimed(maximum,false){if(!limit)throw std::invalid_argument("empty render request budget");}
    uint32_t reserve(){
        std::lock_guard<std::mutex> lock(mutex);
        if(cancelled)throw std::runtime_error("render dispatch cancelled");
        if(issued==limit)throw Exhausted{};
        return issued++;
    }
    void cancel(){
        {std::lock_guard<std::mutex> lock(mutex);cancelled=true;}
        changed.notify_all();
    }
    template<class F> auto execute(uint32_t ticket,Clock::time_point deadline,F operation)->decltype(operation()){
        std::unique_lock<std::mutex> lock(mutex);
        if(ticket>=issued || ticket<serving || claimed[ticket])throw std::invalid_argument("invalid render dispatch ticket");
        claimed[ticket]=true;
        if(!changed.wait_until(lock,deadline,[&]{return cancelled || ticket==serving;})){
            cancelled=true;lock.unlock();changed.notify_all();throw std::runtime_error("render dispatch wait expired");
        }
        if(cancelled)throw std::runtime_error("render dispatch cancelled");
        if(Clock::now()>=deadline){cancelled=true;lock.unlock();changed.notify_all();throw std::runtime_error("render dispatch deadline expired");}
        lock.unlock();
        try {
            auto result=operation();
            lock.lock();++serving;lock.unlock();changed.notify_all();return result;
        } catch(...){cancel();throw;}
    }
};
}
