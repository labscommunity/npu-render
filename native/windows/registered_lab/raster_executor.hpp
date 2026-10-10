#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace nr::raster {
// Already clipped, projected and quantized coordinates in 1/256 pixel units.
// Projection, interpolation, depth, scissoring and blending are separate stages.
using Point = std::array<int32_t, 2>;
struct Sample { std::array<Point, 3> triangle; Point point; };
// Edge-difference magnitudes fit 29 bits; triangle determinants fit signed 59 bits.
constexpr int32_t coordinate_limit = (1 << 28) - 1;
enum class CullMode : uint32_t { none = 0, front = 1, back = 2 };
class CoverageExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    CoverageExecutor(uint32_t samples, const std::string &backend, CullMode cull = CullMode::none, bool front_ccw = false);
    ~CoverageExecutor();
    CoverageExecutor(const CoverageExecutor &) = delete;
    CoverageExecutor &operator=(const CoverageExecutor &) = delete;
    struct Result { std::vector<uint8_t> covered; std::string execution_device; };
    // Both windings, top-left edge ownership, and degenerate-triangle rejection.
    // Scissor bounds use the same 1/256-pixel coordinates as sample positions.
    Result execute(const std::vector<Sample> &samples,
                   const std::optional<std::array<int32_t,4>> &scissor = std::nullopt);
};
}
