#include "provider_compile_statistics.hpp"
#include "solid_rectangle.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace nr::raster;
namespace {
void test(bool value) { if (!value) throw std::runtime_error("solid rectangle contract failed"); }
uint32_t random32() { static uint32_t x = 731941; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
}
int main(int argc, char **argv) try {
    const std::string backend = argc > 1 ? argv[1] : "CPU"; uint64_t checked = 0;
    for (const auto width : {64u, 1024u}) for (const bool bgra : {false, true}) {
        const auto height = width == 64 ? 64u : 768u;
        SolidRectangleExecutor executor(width, height, backend, bgra);
        std::vector<PackedColor> background(size_t(width) * height);
        for (auto &p : background) for (auto &c : p) c = uint8_t(random32());
        for (unsigned frame = 0; frame < 6; ++frame) {
            std::array<int32_t, 4> bounds{0, 0, int32_t(width) * 256, int32_t(height) * 256};
            Color color{.0625f, .25f, .5f, 1};
            if (frame == 0) color = {0, 0, 0, 1};
            if (frame == 1) bounds = {17 * 256 + 64, 13 * 256 + 192, int32_t(width - 3) * 256 + 128, int32_t(height - 7) * 256 + 128};
            if (frame == 2) bounds = {128, 128, 128, 128};
            if (frame == 3) bounds = {-256, -256, 3 * 256, 4 * 256};
            if (frame == 4) bounds = {128, 128, 7 * 256 + 128, 9 * 256 + 128};
            if (frame == 5) color = {127.5f / 255, 126.5f / 255, 1, 0};
            const auto result = executor.execute(bounds, color, background); uint32_t covered = 0;
            for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
                const auto px = int32_t(x) * 256 + 128, py = int32_t(y) * 256 + 128;
                const bool inside = px >= bounds[0] && px < bounds[2] && py >= bounds[1] && py < bounds[3];
                covered += inside;
                for (unsigned c = 0; c < 4; ++c) {
                    const auto source = bgra && c < 3 ? 2 - c : c;
                    volatile float value = color[source] * 255;
                    const auto expected = inside ? uint8_t(std::nearbyint(float(value))) : background[size_t(y) * width + x][c];
                    test(result.pixels[size_t(y) * width + x][c] == expected); ++checked;
                }
            }
            test(result.covered_samples == covered);
            std::printf("{\"solid_rectangle\":true,\"frame\":%u,\"width\":%u,\"height\":%u,\"bgra\":%s,\"covered_samples\":%u,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",
                frame, width, height, bgra ? "true" : "false", covered, result.execution_device.c_str());
        }
        for (const float invalid : {-1.f, 1.001f, std::numeric_limits<float>::quiet_NaN()}) {
            bool rejected = false;
            try { (void)executor.execute({0, 0, 256, 256}, {invalid, 0, 0, 1}, background); }
            catch (const std::invalid_argument &) { rejected = true; } test(rejected);
        }
        test(executor.execute({0, 0, 256, 256}, {0, 0, 0, 1}, background).covered_samples == 1);
    }
    uint64_t blend_checked = 0;
    for (const auto width : {64u,1024u}) for (const bool bgra : {false,true}) {
        const uint32_t height = width == 64 ? 64 : 768;
        SolidRectangleExecutor executor(width,height,backend,bgra,true);
        std::vector<PackedColor> background(size_t(width)*height);
        for(size_t i=0;i<background.size();++i) for(unsigned c=0;c<4;++c) background[i][c]=uint8_t(i+c*31);
        const std::array<int32_t,4> bounds{-256,128,int32_t(width)*256,int32_t(height)*256};
        const std::array<int32_t,4> clip{5,7,int32_t(width)-3,int32_t(height)-5};
        const unsigned desktop_alpha[]{0,255,128,1,254,73};
        const unsigned frames = width == 64 ? 256u : 6u;
        for(unsigned frame=0;frame<frames;++frame) {
            const unsigned alpha = width == 64 ? frame : desktop_alpha[frame];
            const std::array<unsigned,4> bytes{(std::min)(17u,alpha),(std::min)(127u,alpha),(std::min)(231u,alpha),alpha};
            Color color{};for(unsigned c=0;c<4;++c) color[c]=float(bytes[c])/255;
            const auto result=executor.execute(bounds,color,background,clip);
            test(result.covered_samples == unsigned((clip[2]-clip[0])*(clip[3]-clip[1])));
            for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) for(unsigned c=0;c<4;++c) {
                const bool inside=int32_t(x)>=clip[0] && int32_t(x)<clip[2] && int32_t(y)>=clip[1] && int32_t(y)<clip[3];
                const auto old=background[size_t(y)*width+x][c];const auto lane=bgra && c<3 ? 2-c : c;
                const auto expected=inside ? uint8_t((bytes[lane]*255+unsigned(old)*(255-alpha)+127)/255) : old;
                test(result.pixels[size_t(y)*width+x][c]==expected);++blend_checked;
            }
        }
        for(const Color invalid : {Color{.5f,0,0,1},Color{1,0,0,0},Color{0,0,0,.5f}}) {
            bool rejected=false;try{(void)executor.execute(bounds,invalid,background,clip);}catch(const std::invalid_argument &){rejected=true;}test(rejected);
        }
        for(const std::array<int32_t,4> invalid : {std::array<int32_t,4>{-1,0,1,1},std::array<int32_t,4>{0,0,int32_t(width)+1,1},std::array<int32_t,4>{2,0,1,1}}) {
            bool rejected=false;try{(void)executor.execute(bounds,{0,0,0,0},background,invalid);}catch(const std::invalid_argument &){rejected=true;}test(rejected);
        }
        test(executor.execute(bounds,{0,0,0,0},background,clip).pixels==background);
        test(executor.execute(bounds,{0,0,0,1},background,std::array<int32_t,4>{1,1,1,1}).pixels==background);
        std::printf("{\"blended_scissored_rectangle\":true,\"width\":%u,\"height\":%u,\"bgra\":%s,\"opacity_levels_checked\":%u,\"execution_device\":\"%s\"}\n",width,height,bgra?"true":"false",frames,backend.c_str());
    }
    std::printf("{\"blend_checked_components\":%llu,\"scissor_and_premultiplied_byte_color_admission_checked\":true}\n",static_cast<unsigned long long>(blend_checked));
    std::printf("{\"checked_components\":%llu,\"invalid_colors_rejected\":12,\"reusable_after_input_rejection\":true}\n", static_cast<unsigned long long>(checked));
    const auto stats=nr::provider::compile_statistics();
    std::printf("cache calls=%llu requests=%llu hits=%llu unavailable=%llu\n",static_cast<unsigned long long>(stats.calls),static_cast<unsigned long long>(stats.cache_requests),static_cast<unsigned long long>(stats.cache_hits),static_cast<unsigned long long>(stats.cache_status_unavailable));
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
