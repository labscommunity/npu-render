#include "projection_executor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace nr::raster;
#define TEST(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
namespace {
uint32_t random32() {
    static uint32_t state = 0xc4138415;
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
float uniform() { return float(random32() & 0xffffffu) / float(0x1000000u); }
ProjectedVertex reference(const ClipPosition &p, const Viewport &v) {
    // Explicit scalar operations avoid host FMA contraction in the oracle.
    volatile float nx = p[0] / p[3], ny = p[1] / p[3], nz = p[2] / p[3];
    volatile float x = nx + 1, y = 1 - ny;
    x = x * (v.width * .5f); y = y * (v.height * .5f);
    x = x + v.x; y = y + v.y;
    volatile float z = nz * (v.max_depth - v.min_depth); z = z + v.min_depth;
    return {{int32_t(std::nearbyint(float(x) * 256)), int32_t(std::nearbyint(float(y) * 256))},
            {x, y}, z, 1.0f / p[3]};
}
}
int main(int argc, char **argv) try {
    const std::string backend = argc > 1 ? argv[1] : "CPU";
    constexpr uint32_t count = 256;
    ProjectionExecutor executor(count, backend);
    std::vector<ClipPosition> vertices(count);
    Viewport viewport{0, 0, 1024, 768, 0, 1};
    for (unsigned frame = 0; frame < 4; ++frame) {
        viewport = frame % 2 ? Viewport{-.125f, .375f, 8192, 4320, .125f, .875f} : Viewport{0, 0, 1024, 768, 0, 1};
        for (auto &p : vertices) {
            p = {uniform() * 2 - 1, uniform() * 2 - 1, 0, frame >= 2 ? .5f + uniform() * 3.5f : 1};
            p[2] = uniform() * p[3];
        }
        if (!frame) {
            vertices[0] = {-1, 1, 1, 1}; vertices[1] = {1, 1, 1, 1};
            vertices[2] = {1, -1, 1, 1}; vertices[3] = {-1, -1, 1, 1};
        }
        if (frame == 3) {
            viewport = {-.5f, -.5f, 1, 1, .125f, .875f};
            for (uint32_t i = 0; i < count; ++i) {
                const float desired = (float(int(i) - 128) + .5f) / 256;
                vertices[i] = {desired * 2, -desired * 2, .5f, 1};
            }
        }
        const auto result = executor.execute(vertices, viewport);
        TEST(result.vertices.size() == count);
        float screen_error = 0, depth_error = 0, inverse_error = 0;
        uint32_t scalar_snap_differences = 0;
        for (size_t i = 0; i < vertices.size(); ++i) {
            const auto expected = reference(vertices[i], viewport), actual = result.vertices[i];
            if (actual.snapped != expected.snapped) ++scalar_snap_differences;
            if (backend != "CPU" && actual.snapped != expected.snapped) {
                std::fprintf(stderr, "snap mismatch frame=%u vertex=%zu actual=%d,%d expected=%d,%d\n", frame, i,
                    actual.snapped[0], actual.snapped[1], expected.snapped[0], expected.snapped[1]); return 1;
            }
            // D3D11 float-to-fixed allows 0.6 units from the ideal value. CPU
            // reciprocal/fusion can differ from sequential FP32 at a tie; the
            // NPU qualification above additionally requires exact scalar bits.
            const auto &p = vertices[i];
            const double ideal_x = ((double(p[0]) / p[3] + 1) * viewport.width * .5 + viewport.x) * 256;
            const double ideal_y = ((1 - double(p[1]) / p[3]) * viewport.height * .5 + viewport.y) * 256;
            TEST(std::abs(actual.snapped[0] - ideal_x) <= .6 && std::abs(actual.snapped[1] - ideal_y) <= .6);
            for (size_t c = 0; c < 2; ++c) screen_error = (std::max)(screen_error, std::abs(actual.screen[c] - expected.screen[c]));
            depth_error = (std::max)(depth_error, std::abs(actual.depth - expected.depth));
            inverse_error = (std::max)(inverse_error, std::abs(actual.inverse_w - expected.inverse_w));
        }
        TEST((backend == "CPU" || screen_error == 0) && depth_error <= 1.2e-7f && inverse_error <= 1.2e-7f);
        if (frame == 3) for (const auto &p : result.vertices) TEST(!(p.snapped[0] & 1) && !(p.snapped[1] & 1));
        std::printf("{\"projection\":true,\"frame\":%u,\"vertices\":%u,\"scalar_snap_differences\":%u,\"within_fixed_point_tolerance\":true,\"screen_error\":%.9g,\"depth_error\":%.9g,\"inverse_w_error\":%.9g,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",
                    frame, count, scalar_snap_differences, screen_error, depth_error, inverse_error, result.execution_device.c_str());
    }
    const auto valid = vertices[0];
    for (unsigned i = 0; i < 4; ++i) {
        if (i == 0) vertices[0][3] = 0;
        if (i == 1) vertices[0][0] = std::numeric_limits<float>::quiet_NaN();
        if (i == 2) vertices[0][2] = 2;
        if (i == 3) vertices[0][0] = 100000;
        bool rejected = false;
        try { (void)executor.execute(vertices, viewport); } catch (const std::invalid_argument &) { rejected = true; }
        TEST(rejected); vertices[0] = valid;
    }
    TEST(executor.execute(vertices, viewport).vertices[0].snapped == reference(valid, viewport).snapped);
    std::puts("{\"invalid_projection_requests_rejected\":4,\"reusable_after_input_rejection\":true}");
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
