#pragma once
#include <atomic>
#include <cstdint>
namespace nr::registered_lab {
inline uint64_t diagnostic_identity() noexcept {static std::atomic<uint64_t> next{0};return next.fetch_add(1,std::memory_order_relaxed)+1;}
}
