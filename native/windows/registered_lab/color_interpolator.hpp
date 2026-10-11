#pragma once
#include "projection_executor.hpp"
#include "output_merger.hpp"
namespace nr::raster {
// Interpolation plus the identity float4 pixel shader. Coverage is supplied by
// the exact integer rasterizer. No host interpolation or color arithmetic.
class ColorInterpolator {
    struct State;
    std::unique_ptr<State> state;
public:
    ColorInterpolator(uint32_t samples, const std::string &backend, bool perspective, bool per_sample_triangle=false);
    ~ColorInterpolator();
    struct Result { std::vector<Color> colors; std::string execution_device; };
    Result execute(const std::array<ProjectedVertex,3> &triangle, const std::array<Color,3> &colors,
                   const std::vector<Point> &points, const std::vector<uint8_t> &coverage);
    Result execute(const std::vector<std::array<ProjectedVertex,3>> &triangles,
                   const std::vector<std::array<Color,3>> &colors,
                   const std::vector<Point> &points, const std::vector<uint8_t> &coverage);
};
}
