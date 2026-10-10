#include "fifo_dispatch.hpp"
#include <atomic>
#include <future>
#include <thread>
#include <vector>
#include <cstdio>
using nr::registered_lab::FifoDispatch;
#define TEST(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main()try{
    FifoDispatch queue(4);std::vector<uint32_t> tickets;
    for(unsigned i=0;i<4;++i)tickets.push_back(queue.reserve());
    bool exhausted=false;try{queue.reserve();}catch(const FifoDispatch::Exhausted &){exhausted=true;}TEST(exhausted);
    std::atomic<int> active{0};std::vector<uint32_t> order;std::vector<std::future<uint32_t>> results;
    // Start in reverse reservation order. Only ticket zero may enter first,
    // and every provider invocation must finish before the next enters.
    for(int i=3;i>=0;--i)results.push_back(std::async(std::launch::async,[&,i]{
        return queue.execute(tickets[size_t(i)],FifoDispatch::Clock::now()+std::chrono::seconds(5),[&,i]{
            TEST(active.fetch_add(1)==0);order.push_back(uint32_t(i));TEST(active.fetch_sub(1)==1);return uint32_t(i);
        });
    }));
    for(size_t i=0;i<4;++i)TEST(results[i].get()==3-i);
    TEST(order==std::vector<uint32_t>({0,1,2,3}));
    FifoDispatch duplicate(1);const auto unique=duplicate.reserve();std::promise<void> entered,release;
    auto release_future=release.get_future();
    auto running=std::async(std::launch::async,[&]{return duplicate.execute(unique,FifoDispatch::Clock::now()+std::chrono::seconds(5),[&]{entered.set_value();release_future.wait();return 1;});});
    entered.get_future().wait();bool duplicate_rejected=false;
    try{duplicate.execute(unique,FifoDispatch::Clock::now()+std::chrono::seconds(1),[]{return 2;});}catch(const std::invalid_argument &){duplicate_rejected=true;}
    release.set_value();TEST(running.get()==1 && duplicate_rejected);
    bool stale=false;try{queue.execute(0,FifoDispatch::Clock::now(),[]{return 0;});}catch(const std::invalid_argument &){stale=true;}TEST(stale);
    FifoDispatch stopped(2);stopped.reserve();const auto waiting=stopped.reserve();
    auto pending=std::async(std::launch::async,[&]{
        try{stopped.execute(waiting,FifoDispatch::Clock::now()+std::chrono::seconds(5),[]{return 0;});return false;}
        catch(const std::runtime_error &){return true;}
    });stopped.cancel();TEST(pending.wait_for(std::chrono::seconds(1))==std::future_status::ready && pending.get());
    FifoDispatch expired(2);expired.reserve();const auto late=expired.reserve();bool timed_out=false;
    try{expired.execute(late,FifoDispatch::Clock::now(),[]{return 0;});}catch(const std::runtime_error &){timed_out=true;}TEST(timed_out);
    FifoDispatch elapsed(1);const auto elapsed_ticket=elapsed.reserve();bool executed=false;timed_out=false;
    try{elapsed.execute(elapsed_ticket,FifoDispatch::Clock::now(),[&]{executed=true;return 0;});}catch(const std::runtime_error &){timed_out=true;}TEST(timed_out && !executed);
    FifoDispatch failed(2);const auto first=failed.reserve(),second=failed.reserve();bool rejected=false;
    try{failed.execute(first,FifoDispatch::Clock::now()+std::chrono::seconds(1),[]()->int{throw std::runtime_error("provider failed");});}catch(const std::runtime_error &){rejected=true;}TEST(rejected);
    rejected=false;try{failed.execute(second,FifoDispatch::Clock::now()+std::chrono::seconds(1),[]{return 0;});}catch(const std::runtime_error &){rejected=true;}TEST(rejected);
    std::puts("{\"fifo_order\":true,\"single_provider_owner\":true,\"bounded_requests\":true,\"cancellation_and_failure_wake_waiters\":true,\"desktop_offload_tested\":false}");return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
