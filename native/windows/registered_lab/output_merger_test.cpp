#include "output_merger.hpp"
#include "blend_reference.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace nr::raster;
#define TEST(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
namespace {
uint32_t random32() {
    static uint32_t state = 0x612448fe;
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
float uniform() { return float(random32() & 0xffffffu) / float(0x1000000u); }
}
int main(int argc, char **argv) try {
    const std::string backend = argc > 1 ? argv[1] : "CPU";
    constexpr uint32_t count = 512;
    std::vector<Color> colors(count);
    std::vector<PackedColor> background(count);
    std::vector<uint8_t> coverage(count);
    uint64_t checked = 0;
    for (uint32_t mode=0;mode<blend_factors.size();++mode) for (const bool bgra : {false, true}) for (const uint8_t mask : {uint8_t(15), uint8_t(5), uint8_t(10)}) {
        const auto blend=BlendMode(mode);OutputMerger merger(count, backend, blend, bgra, mask);
        for (unsigned frame = 0; frame < 4; ++frame) {
            for (size_t i = 0; i < count; ++i) {
                for (unsigned c = 0; c < 4; ++c) {
                    colors[i][c] = uniform() * 1.25f - .125f;
                    background[i][c] = uint8_t(random32());
                }
                colors[i][3] = uniform()*1.5f-.25f; coverage[i] = uint8_t(random32() & 1);
                if (frame == 0) { colors[i] = {0, 0, 0, 1}; coverage[i] = 1; }
                if (frame == 1) coverage[i] = 0;
                if (frame == 2) colors[i][3] = 1;
            }
            const auto result = merger.execute(colors, background, coverage);
            TEST(result.pixels.size() == count);
            uint32_t scalar_differences = 0;
            for (size_t i = 0; i < count; ++i) for (unsigned channel = 0; channel < 4; ++channel) {
                const auto source = bgra && channel < 3 ? 2 - channel : channel;
                const auto actual = result.pixels[i][channel], old = background[i][channel];
                if (!coverage[i] || !(mask & (1u << source))) TEST(actual == old);
                else {
                    const auto terms=reference_blend_terms(blend,std::clamp(colors[i][source],0.f,1.f),std::clamp(colors[i][3],0.f,1.f),float(old),float(background[i][3]));
                    volatile float foreground=terms[0],contribution=terms[1];
                    volatile float value=foreground+contribution;
                    const auto expected=uint8_t(std::nearbyint(std::clamp(float(value),0.f,255.f)));
                    if(actual!=expected)++scalar_differences;
                    const auto precise=reference_blend_terms(blend,std::clamp(double(colors[i][source]),0.,1.),std::clamp(double(colors[i][3]),0.,1.),double(old),double(background[i][3]));
                    const double ideal=std::clamp(precise[0]+precise[1],0.,255.);
                    if (std::abs(double(actual)-ideal)>.6 || (backend=="NPU" && actual!=expected)) {
                        std::fprintf(stderr, "merger mismatch blend=%u bgra=%u mask=%u frame=%u sample=%zu channel=%u actual=%u expected=%u ideal=%.9g\n",
                                     unsigned(blend), unsigned(bgra), unsigned(mask), frame, i, channel, unsigned(actual), unsigned(expected),ideal); return 1;
                    }
                }
                ++checked;
            }
            std::printf("{\"output_merger\":true,\"blend_mode\":%u,\"bgra\":%s,\"write_mask\":%u,\"frame\":%u,\"pixels\":%u,\"scalar_byte_differences\":%u,\"masked_background_exact\":true,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",
                unsigned(blend), bgra ? "true" : "false", unsigned(mask), frame, count, scalar_differences, result.execution_device.c_str());
        }
        const auto valid = colors[0];
        colors[0][0] = std::numeric_limits<float>::quiet_NaN();
        bool rejected = false;
        try { (void)merger.execute(colors, background, coverage); } catch (const std::invalid_argument &) { rejected = true; }
        TEST(rejected); colors[0] = valid;
        const auto valid_coverage = coverage[0]; coverage[0] = 2; rejected = false;
        try { (void)merger.execute(colors, background, coverage); } catch (const std::invalid_argument &) { rejected = true; }
        TEST(rejected); coverage[0] = valid_coverage;
        TEST(merger.execute(colors, background, coverage).pixels.size() == count);
    }
    std::printf("{\"checked_output_components\":%llu,\"invalid_requests_rejected\":156,\"reusable_after_input_rejection\":true}\n",
                static_cast<unsigned long long>(checked));
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
