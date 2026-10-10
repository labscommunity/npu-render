#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace nr::raster {
// Already clipped, projected and quantized coordinates in 1/256 pixel units.
// Projection, interpolation, depth, scissoring and blending are separate stages.
using Point = std::array<int32_t, 2>;
struct Sample { std::array<Point, 3> triangle; Point point; };
constexpr int32_t coordinate_limit = (1 << 22) - 1;
class CoverageExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    CoverageExecutor(uint32_t samples, const std::string &backend);
    ~CoverageExecutor();
    CoverageExecutor(const CoverageExecutor &) = delete;
    CoverageExecutor &operator=(const CoverageExecutor &) = delete;
    struct Result { std::vector<uint8_t> covered; std::string execution_device; };
    // Both windings, top-left edge ownership, and degenerate-triangle rejection.
    Result execute(const std::vector<Sample> &samples);
};
}
