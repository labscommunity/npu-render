#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
namespace nr::registered_lab {
inline uint64_t client_qpc() noexcept {
 LARGE_INTEGER value{};return QueryPerformanceCounter(&value)&&value.QuadPart>0?uint64_t(value.QuadPart):0;
}
inline uint64_t client_qpc_frequency() noexcept {
 static const uint64_t frequency=[](){LARGE_INTEGER value{};return QueryPerformanceFrequency(&value)&&value.QuadPart>0?uint64_t(value.QuadPart):uint64_t(0);}();
 return frequency;
}
struct ClientTraceTiming {
 std::atomic<uint64_t> admission_calls{0},admission_ticks{0},trace_calls{0},trace_ticks{0};
};
inline ClientTraceTiming &client_trace_timing(){static ClientTraceTiming value;return value;}
struct ClientTimingScope {
 std::atomic<uint64_t> &calls,&ticks;uint64_t start;
 ClientTimingScope(std::atomic<uint64_t> &c,std::atomic<uint64_t> &t) noexcept:calls(c),ticks(t),start(client_qpc()){}
 ~ClientTimingScope(){const auto end=client_qpc();calls.fetch_add(1,std::memory_order_relaxed);if(start&&end>=start)ticks.fetch_add(end-start,std::memory_order_relaxed);}
};
}
