#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace nr {
struct VisibleSpan {
    size_t source_pixel, destination_pixel, pixels;
};
// Visit layers from front to back. Fully opaque pixels cover lower layers;
// partial alpha never hides a lower contribution. Coordinates are physical pixels.
inline std::vector<VisibleSpan> visible_spans(const uint8_t *alpha, size_t source_width,
                                              size_t source_height, int left, int top, size_t width,
                                              size_t height, std::vector<uint8_t> &covered) {
    if (!alpha || !width || !height || width > 16384 || height > 16384 || !source_width ||
        !source_height || source_width > 16384 || source_height > 16384 ||
        covered.size() != width * height || left < -32768 || left > 32768 || top < -32768 ||
        top > 32768)
        throw std::invalid_argument("invalid visibility surface");
    std::vector<VisibleSpan> result;
    const int x0 = std::max(0, left), y0 = std::max(0, top);
    const int x1 = std::min(int(width), left + int(source_width));
    const int y1 = std::min(int(height), top + int(source_height));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const size_t src = size_t(y - top) * source_width + size_t(x - left);
            const size_t dst = size_t(y) * width + size_t(x);
            const auto a = alpha[src];
            if (!covered[dst] && a) {
                if (!result.empty() && result.back().source_pixel + result.back().pixels == src &&
                    result.back().destination_pixel + result.back().pixels == dst)
                    ++result.back().pixels;
                else
                    result.push_back({src, dst, 1});
            }
            if (a == 255)
                covered[dst] = 1;
        }
    return result;
}
} // namespace nr
