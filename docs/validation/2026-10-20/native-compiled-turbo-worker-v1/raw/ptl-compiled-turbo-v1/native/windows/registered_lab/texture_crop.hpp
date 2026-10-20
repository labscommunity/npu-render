#pragma once
#include "output_merger.hpp"
namespace nr::raster {
// Exact single-mip point sampling for an in-bounds, integer-translated opaque
// rectangle. The draw boundary must prove that the shader reduces to this form.
class TextureCropExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    TextureCropExecutor(uint32_t source_width, uint32_t source_height, uint32_t width, uint32_t height,
                        const std::string &backend, bool swap_red_blue);
    ~TextureCropExecutor();
    TextureCropExecutor(const TextureCropExecutor &) = delete;
    TextureCropExecutor &operator=(const TextureCropExecutor &) = delete;
    struct Result { std::vector<PackedColor> pixels; std::string execution_device; };
    Result execute(uint32_t x, uint32_t y, const std::vector<PackedColor> &source);
};
}
