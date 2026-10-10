#pragma once
#include "output_merger.hpp"
#include "raster_executor.hpp"
namespace nr::raster {
// Exact single-sample rectangle coverage and unblended uniform-color writes.
// Bounds are snapped 1/256-pixel coordinates, with top/left inclusion. Color
// quantization runs once; exact integer-valued FP16 selects image bytes.
class SolidRectangleExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    SolidRectangleExecutor(uint32_t width, uint32_t height, const std::string &backend, bool bgra);
    ~SolidRectangleExecutor();
    SolidRectangleExecutor(const SolidRectangleExecutor &) = delete;
    SolidRectangleExecutor &operator=(const SolidRectangleExecutor &) = delete;
    struct Result { std::vector<PackedColor> pixels; uint32_t covered_samples; std::string execution_device; };
    Result execute(const std::array<int32_t, 4> &bounds, const Color &color, const std::vector<PackedColor> &background);
};
}
