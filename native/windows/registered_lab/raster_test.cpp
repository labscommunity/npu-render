#include "raster_executor.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
using namespace nr::raster;
#define TEST(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
namespace {
int64_t edge(Point a, Point b, Point p) {
    return (int64_t(b[0]) - a[0]) * (int64_t(p[1]) - a[1]) -
           (int64_t(b[1]) - a[1]) * (int64_t(p[0]) - a[0]);
}
bool reference(Sample sample) {
    auto &v = sample.triangle;
    const auto area = edge(v[0], v[1], v[2]);
    if (!area) return false;
    if (area < 0) std::swap(v[1], v[2]);
    for (size_t i = 0; i < 3; ++i) {
        const auto a = v[i], b = v[(i + 1) % 3];
        const auto e = edge(a, b, sample.point);
        const bool top_left = b[1] < a[1] || (b[1] == a[1] && b[0] > a[0]);
        if (e < 0 || (!e && !top_left)) return false;
    }
    return true;
}
uint32_t random32() {
    static uint32_t state = 0x71a041ed;
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
int32_t coordinate() { return int32_t(random32() % uint32_t(coordinate_limit * 2 + 1)) - coordinate_limit; }
}
int main(int argc, char **argv) try {
    const std::string backend = argc > 1 ? argv[1] : "CPU";
    constexpr uint32_t count = 4096;
    CoverageExecutor executor(count, backend);
    std::vector<Sample> samples(count);
    uint64_t checked = 0;
    for (unsigned frame = 0; frame < 8; ++frame) {
        for (auto &s : samples) {
            for (auto &p : s.triangle) p = {coordinate(), coordinate()};
            s.point = {coordinate(), coordinate()};
        }
        for (uint32_t i = 0; i < count; ++i) {
            auto &s = samples[i];
            if (frame < 2) {
                s.triangle = (i / 1024) % 2 ? std::array<Point, 3>{{{8192, 0}, {8192, 8192}, {0, 8192}}}
                    : std::array<Point, 3>{{{0, 0}, {8192, 0}, {0, 8192}}};
                s.point = {int32_t(i % 32) * 256 + 128, int32_t((i / 32) % 32) * 256 + 128};
                if (frame) std::swap(s.triangle[1], s.triangle[2]);
            } else if (frame == 2) s.point = s.triangle[i % 3];
            else if (frame == 3) s.triangle[2] = s.triangle[1];
            else if (frame == 4) {
                // Large opposite-signed coordinates and an exact central diagonal.
                const auto k = coordinate_limit;
                s.triangle = {{{-k, -k}, {k, -k}, {k, k}}};
                const auto p = coordinate(); s.point = {p, p};
                if (i & 1) std::swap(s.triangle[0], s.triangle[1]);
            }
        }
        const auto result = executor.execute(samples);
        TEST(result.covered.size() == count);
        uint32_t covered = 0;
        for (size_t i = 0; i < count; ++i) {
            if (bool(result.covered[i]) != reference(samples[i])) {
                std::fprintf(stderr, "coverage mismatch frame=%u sample=%zu\n", frame, i); return 1;
            }
            covered += result.covered[i]; ++checked;
        }
        if (frame < 2) for (size_t i = 0; i < 1024; ++i)
            TEST(result.covered[i] + result.covered[i + 1024] == 1);
        std::printf("{\"triangle_coverage\":true,\"frame\":%u,\"samples\":%u,\"covered\":%u,\"exact\":true,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",
                    frame, count, covered, result.execution_device.c_str());
    }
    const auto valid = samples[0];
    for (const auto bad : {INT32_MIN, -coordinate_limit - 1, coordinate_limit + 1, INT32_MAX}) {
        samples[0].point[0] = bad;
        bool rejected = false;
        try { (void)executor.execute(samples); } catch (const std::invalid_argument &) { rejected = true; }
        TEST(rejected); samples[0] = valid;
        samples[0].triangle[2][1] = bad; rejected = false;
        try { (void)executor.execute(samples); } catch (const std::invalid_argument &) { rejected = true; }
        TEST(rejected); samples[0] = valid;
    }
    const auto after = executor.execute(samples);
    for (size_t i = 0; i < count; ++i) TEST(bool(after.covered[i]) == reference(samples[i]));
    uint64_t cull_checked=0;
    const std::array<int32_t,4> scissor{5*256,7*256,29*256,25*256};
    for(const auto cull:{CullMode::none,CullMode::front,CullMode::back})for(const bool ccw:{false,true}) {
        CoverageExecutor culled(count,backend,cull,ccw);
        for(size_t i=0;i<count;++i){
            auto &s=samples[i];s.triangle={{{0,0},{8192,0},{0,8192}}};
            if(i&1)std::swap(s.triangle[1],s.triangle[2]);
            if(i%7==0)s.triangle[2]=s.triangle[1];
            s.point={int32_t((i/2)%32)*256+128,int32_t((i/64)%32)*256+128};
        }
        const auto result=culled.execute(samples,scissor);
        for(size_t i=0;i<count;++i){
            const auto &s=samples[i];const auto area=edge(s.triangle[0],s.triangle[1],s.triangle[2]);
            const bool front=(area<0)==ccw;
            const bool retained=cull==CullMode::none || (cull==CullMode::back?front:!front);
            const bool clipped=s.point[0]>=scissor[0] && s.point[1]>=scissor[1] && s.point[0]<scissor[2] && s.point[1]<scissor[3];
            TEST(bool(result.covered[i])==(reference(s) && retained && clipped));++cull_checked;
        }
    }
    std::printf("{\"culling_and_scissor_samples\":%llu,\"exact\":true,\"all_face_modes\":true}\n",static_cast<unsigned long long>(cull_checked));
    std::printf("{\"checked_samples\":%llu,\"invalid_coordinate_requests_rejected\":8,\"request_reusable_after_input_rejection\":true}\n",
                static_cast<unsigned long long>(checked + count));
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
