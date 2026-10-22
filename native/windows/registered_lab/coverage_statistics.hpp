#pragma once
#include <cstdint>
namespace nr::raster {
struct CoverageStatistics {uint64_t compact_calls=0,compact_samples=0,wide_calls=0;};
inline thread_local uint64_t compact_coverage_calls=0,compact_coverage_samples=0,wide_coverage_calls=0;
inline CoverageStatistics coverage_statistics(){return {compact_coverage_calls,compact_coverage_samples,wide_coverage_calls};}
}
