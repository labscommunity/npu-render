#include "../include/visible_spans.hpp"
#include <array>
#include <iostream>

struct Layer {
    int x, y;
    std::vector<uint8_t> pixels, alpha;
};
int main() {
    try {
        constexpr size_t width = 17, height = 13, sw = 11, sh = 9;
        size_t cases = 0, avoided = 0;
        for (int left : {-12, -3, 0, 7, 16, 20})
            for (int top : {-10, -2, 4, 12}) {
                std::array<Layer, 3> layers;
                for (size_t l = 0; l < layers.size(); ++l) {
                    auto &layer = layers[l];
                    layer.x = left + int(l);
                    layer.y = top - int(l);
                    layer.pixels.resize(sw * sh * 4);
                    layer.alpha.resize(sw * sh);
                    for (size_t i = 0; i < sw * sh; ++i) {
                        auto a = uint8_t((i + l) % 3 == 0 ? 0 : (i + l) % 3 == 1 ? 128 : 255);
                        layer.alpha[i] = a;
                        for (size_t c = 0; c < 4; ++c)
                            layer.pixels[i * 4 + c] =
                                c == 3 ? a : uint8_t((i * 7 + c * 17 + l * 23) % (a + 1));
                    }
                }
                std::vector<uint8_t> expected(width * height * 4, 43), actual = expected,
                                                                       covered(width * height);
                std::array<std::vector<nr::VisibleSpan>, 3> plan;
                for (size_t l = 0; l < layers.size(); ++l) {
                    const auto &layer = layers[l];
                    plan[l] = nr::visible_spans(layer.alpha.data(), sw, sh, layer.x, layer.y, width,
                                                height, covered);
                }
                auto over = [](const uint8_t *f, uint8_t *b) {
                    for (size_t c = 0; c < 4; ++c)
                        b[c] = uint8_t(std::min(
                            255u, unsigned(f[c]) + (unsigned(b[c]) * (255 - f[3]) + 127) / 255));
                };
                size_t full = 0, selected = 0;
                for (size_t l = layers.size(); l-- > 0;) {
                    const auto &layer = layers[l];
                    for (size_t y = 0; y < sh; ++y)
                        for (size_t x = 0; x < sw; ++x) {
                            int dx = layer.x + int(x), dy = layer.y + int(y);
                            if (dx < 0 || dy < 0 || dx >= int(width) || dy >= int(height))
                                continue;
                            over(layer.pixels.data() + (y * sw + x) * 4,
                                 expected.data() + (size_t(dy) * width + dx) * 4);
                            ++full;
                        }
                    for (const auto &span : plan[l])
                        for (size_t i = 0; i < span.pixels; ++i) {
                            over(layer.pixels.data() + (span.source_pixel + i) * 4,
                                 actual.data() + (span.destination_pixel + i) * 4);
                            ++selected;
                        }
                }
                if (actual != expected || selected > full)
                    throw std::runtime_error("visibility differs from full scene oracle");
                avoided += full - selected;
                ++cases;
            }
        if (!avoided)
            throw std::runtime_error("test did not exercise occlusion");
        std::cout << "{\"cases\":" << cases << ",\"avoided_pixels\":" << avoided << "}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
