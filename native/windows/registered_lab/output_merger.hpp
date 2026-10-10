#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace nr::raster {
using Color = std::array<float, 4>;
using PackedColor = std::array<uint8_t, 4>;
// Single-sample RGBA8/BGRA8, optional premultiplied source-over, channel masks.
class OutputMerger {
    struct State;
    std::unique_ptr<State> state;
public:
    OutputMerger(uint32_t samples, const std::string &backend, bool blend, bool bgra, uint8_t write_mask = 15);
    ~OutputMerger();
    OutputMerger(const OutputMerger &) = delete;
    OutputMerger &operator=(const OutputMerger &) = delete;
    struct Result { std::vector<PackedColor> pixels; std::string execution_device; };
    Result execute(const std::vector<Color> &colors, const std::vector<PackedColor> &background,
                   const std::vector<uint8_t> &coverage);
};
}
