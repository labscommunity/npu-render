#include "texture_crop.hpp"
#include <cstdio>
#include <random>
#include <chrono>
namespace { void check(bool value) { if (!value) throw std::runtime_error("texture crop contract failed"); } }
int main(int argc, char **argv) try {
    const std::string backend = argc == 2 ? argv[1] : "CPU"; check(argc <= 2);
    std::mt19937 random(7123); uint64_t checked = 0; uint32_t invalid = 0;
    for (const auto dimensions : {std::array<uint32_t,4>{96,80,64,64}, std::array<uint32_t,4>{2304,800,1024,768}}) {
        const auto sw = dimensions[0], sh = dimensions[1], w = dimensions[2], h = dimensions[3];
        for (bool swap : {false, true}) {
            nr::raster::TextureCropExecutor executor(sw, sh, w, h, backend, swap);
            for (uint32_t frame = 0; frame < 4; ++frame) {
                std::vector<nr::raster::PackedColor> source(size_t(sw) * sh);
                for (auto &pixel : source) for (auto &c : pixel) c = uint8_t(random() & 255);
                const auto x = frame & 1 ? 0u : sw - w, y = frame & 2 ? sh - h : 0u;
                const auto start = std::chrono::steady_clock::now(); const auto result = executor.execute(x, y, source);
                const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                check(result.execution_device == backend || result.execution_device.rfind(backend + ".", 0) == 0);
                for (uint32_t row = 0; row < h; ++row) for (uint32_t col = 0; col < w; ++col) {
                    const auto expected = source[size_t(row + y) * sw + col + x]; const auto actual = result.pixels[size_t(row) * w + col];
                    for (uint32_t c = 0; c < 3; ++c) check(actual[c] == expected[swap ? 2 - c : c]); check(actual[3] == 255); checked += 4;
                }
                if (!frame) for (const auto bad : {std::array<uint32_t,2>{sw - w + 1, 0}, std::array<uint32_t,2>{0, sh - h + 1}, std::array<uint32_t,2>{UINT32_MAX,0}}) {
                    bool rejected = false; try { (void)executor.execute(bad[0], bad[1], source); } catch (const std::invalid_argument &) { rejected = true; }
                    check(rejected); ++invalid;
                }
                std::printf("{\"width\":%u,\"height\":%u,\"swap_red_blue\":%s,\"frame\":%u,\"elapsed_ms\":%.3f,\"execution_device\":\"%s\",\"exact\":true}\n", w,h,swap ? "true" : "false",frame,elapsed,result.execution_device.c_str()); std::fflush(stdout);
            }
        }
    }
    std::printf("{\"verified_components\":%llu,\"invalid_offsets_rejected\":%u,\"reused_after_rejection\":true,\"desktop_offload_tested\":false,\"displayed_frames\":0}\n", static_cast<unsigned long long>(checked), invalid);
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
