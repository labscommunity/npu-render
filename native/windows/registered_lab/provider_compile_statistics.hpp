#pragma once
#include <atomic>
#include <cstdint>
namespace nr::provider {
struct CompileStatistics {uint64_t calls=0,cache_requests=0,cache_hits=0,cache_status_unavailable=0;};
inline std::atomic<uint64_t> compile_calls{0},cache_requests{0},cache_hits{0},cache_status_unavailable{0};
inline CompileStatistics compile_statistics(){return {compile_calls.load(),cache_requests.load(),cache_hits.load(),cache_status_unavailable.load()};}
}
