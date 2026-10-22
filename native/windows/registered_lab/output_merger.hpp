#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace nr::raster {
using Color = std::array<float, 4>;
using PackedColor = std::array<uint8_t, 4>;
enum class BlendMode { source_copy, source_over, destination_in, destination_out,
    destination_over, source_in, source_out, source_atop, destination_atop,
    exclusive_or, plus, destination_copy, clear, component_coverage, dual_source_over, invert_destination_over };
enum class BlendFactor { zero, one, source_alpha, inverse_source_alpha, destination_alpha, inverse_destination_alpha };
// Provider-independent Porter-Duff factors, in stable BlendMode order.
inline constexpr std::array<std::array<BlendFactor,2>,13> blend_factors{{
    {BlendFactor::one,BlendFactor::zero}, {BlendFactor::one,BlendFactor::inverse_source_alpha},
    {BlendFactor::zero,BlendFactor::source_alpha}, {BlendFactor::zero,BlendFactor::inverse_source_alpha},
    {BlendFactor::inverse_destination_alpha,BlendFactor::one}, {BlendFactor::destination_alpha,BlendFactor::zero},
    {BlendFactor::inverse_destination_alpha,BlendFactor::zero}, {BlendFactor::destination_alpha,BlendFactor::inverse_source_alpha},
    {BlendFactor::inverse_destination_alpha,BlendFactor::source_alpha}, {BlendFactor::inverse_destination_alpha,BlendFactor::inverse_source_alpha},
    {BlendFactor::one,BlendFactor::one}, {BlendFactor::zero,BlendFactor::one}, {BlendFactor::zero,BlendFactor::zero}
}};
// Component coverage: source * constant + destination * (1 - source),
// independently for RGBA. This also describes RGB glyph masks with alpha blend.
// Dual-source over: source * alpha1 + destination * (1 - alpha1) with alpha1 the pixel shader's second output
// (SRC1_ALPHA / INV_SRC1_ALPHA, DWM's caption text). Only the frame kernel blends it; the per-draw
// executors (valid_blend) stop at component_coverage.
constexpr bool valid_blend(BlendMode mode) { return uint32_t(mode)<=uint32_t(BlendMode::component_coverage); }
// Invert destination over (SrcBlend INV_DEST_COLOR, DestBlend INV_SRC_ALPHA, alpha ZERO / ONE; probe v119):
// colour = source * (1 - destination) + destination * (1 - alpha), alpha = destination. Kernel only.
constexpr bool kernel_blend(BlendMode mode) { return uint32_t(mode)<=uint32_t(BlendMode::invert_destination_over); }
constexpr bool extended_blend(BlendMode mode) { return uint32_t(mode)>=4 && valid_blend(mode); }
constexpr bool general_blend(BlendMode mode) { return mode!=BlendMode::source_copy && mode!=BlendMode::source_over; }
constexpr bool destination_blend(BlendMode mode) { return mode == BlendMode::destination_in || mode == BlendMode::destination_out; }
constexpr BlendMode source_blend(bool enabled) { return enabled ? BlendMode::source_over : BlendMode::source_copy; }
// Single-sample RGBA8/BGRA8 with explicit blend operation and channel masks.
class OutputMerger {
    struct State;
    std::unique_ptr<State> state;
public:
    OutputMerger(uint32_t samples, const std::string &backend, bool blend, bool bgra, uint8_t write_mask = 15);
    OutputMerger(uint32_t samples, const std::string &backend, BlendMode blend, bool bgra, uint8_t write_mask = 15);
    ~OutputMerger();
    OutputMerger(const OutputMerger &) = delete;
    OutputMerger &operator=(const OutputMerger &) = delete;
    struct Result { std::vector<PackedColor> pixels; std::string execution_device; };
    Result execute(const std::vector<Color> &colors, const std::vector<PackedColor> &background,
                   const std::vector<uint8_t> &coverage,
                   const Color &blend_constant = Color{1,1,1,1});
};
}
