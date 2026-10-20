#pragma once
#include <atomic>
#include <cstdint>
#include <chrono>
namespace nr::provider {
struct CompileStatistics {uint64_t calls=0,cache_requests=0,cache_hits=0,cache_status_unavailable=0,backend_calls=0,memory_requests=0,memory_hits=0,memory_evictions=0,setup_ns=0,backend_ns=0;};
inline std::atomic<uint64_t> compile_calls{0},cache_requests{0},cache_hits{0},cache_status_unavailable{0},backend_compile_calls{0},memory_cache_requests{0},memory_cache_hits{0},memory_cache_evictions{0},provider_setup_ns{0},backend_compile_ns{0};
inline CompileStatistics compile_statistics(){return {compile_calls.load(),cache_requests.load(),cache_hits.load(),cache_status_unavailable.load(),backend_compile_calls.load(),memory_cache_requests.load(),memory_cache_hits.load(),memory_cache_evictions.load(),provider_setup_ns.load(),backend_compile_ns.load()};}
class CompileTimer {
    std::atomic<uint64_t> &total;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
public:
    explicit CompileTimer(std::atomic<uint64_t> &counter):total(counter){}
    ~CompileTimer(){total.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count()),std::memory_order_relaxed);}
};
}
