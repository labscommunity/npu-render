#include "npu_render.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }

int main(int argc, char **argv) {
    nr_context *context = nullptr;
    try {
        const char *device = argc > 1 ? argv[1] : "CPU";
        char error[2048];
        context = nr_create(device, 32, error, sizeof(error));
        require(context != nullptr, error);
        constexpr unsigned W = 71, H = 39, STRIDE = W*4+12;
        std::vector<uint8_t> image(STRIDE*H, 0x69), original(STRIDE*H, 0xa5), output;
        for (size_t y = 0; y < H; ++y) for (size_t x = 0; x < W; ++x) {
            unsigned a = unsigned((x*31+y*19)%256);
            for (size_t c = 0; c < 4; ++c) {
                image[y*STRIDE+x*4+c] = uint8_t(c == 3 ? a : (x*13+y*7+c*37)%(a+1));
                original[y*STRIDE+x*4+c] = uint8_t(c == 3 ? 255 : (x*5+y*11+c*17)%256);
            }
        }
        nr_surface source{image.data(), image.size(), STRIDE, W, H, 0};
        const nr_damage_rect regions[] = {{2,3,33,19}, {39,1,30,32}};
        constexpr size_t count = 33*19+30*32;
        std::vector<nr_sample_point> points(count);
        std::vector<bool> touched(STRIDE*H, false);
        for (const auto &r : regions) for (size_t y = 0; y < r.height; ++y)
            for (size_t x = 0; x < r.width; ++x) for (size_t c = 0; c < 4; ++c)
                touched[(r.y+y)*STRIDE+(r.x+x)*4+c] = true;
        for (size_t i = 0; i < count; ++i) {
            unsigned wx = unsigned(i*19%128), wy = unsigned(i*37%128);
            unsigned coefficients[] = {(128-wx)*(128-wy), wx*(128-wy), (128-wx)*wy, wx*wy};
            for (size_t k = 0; k < 4; ++k) {
                points[i].index[k] = uint32_t((i*47+k*(W*H-1))%(W*H));
                points[i].weight[k] = i%31 == 0 ? 0.f : float(coefficients[k])/16384;
            }
        }
        output = original;
        nr_surface destination{output.data(), output.size(), STRIDE, W, H, 0};
        nr_frame_stats stats{};
        require(nr_sample_regions(context,&source,&destination,regions,2,points.data(),points.size(),
                    NR_SOURCE_OVER,1,&stats,error,sizeof(error)) != 0, "unprepared sampling must fail");
        require(output == original, "unprepared sampling modified destination");
        require(nr_prepare_sampling(context,W,H,error,sizeof(error)) == 0, error);
        require(nr_switch(context,"AUTO",error,sizeof(error)) != 0, "AUTO must not silently route devices");
        int maximum = 0;
        for (int operation = 0; operation < 2; ++operation) for (float opacity : {1.f, 0.5f, 0.f}) {
            output = original; destination.data = output.data();
            auto expected = original;
            size_t index = 0;
            for (const auto &r : regions) for (size_t y = 0; y < r.height; ++y) for (size_t x = 0; x < r.width; ++x, ++index) {
                double sampled[4]{};
                for (size_t k = 0; k < 4; ++k) {
                    const auto address = points[index].index[k];
                    for (size_t c = 0; c < 4; ++c)
                        sampled[c] += image[(address/W)*STRIDE+(address%W)*4+c]*double(points[index].weight[k])*opacity;
                }
                for (size_t c = 0; c < 4; ++c) {
                    const size_t into = (r.y+y)*STRIDE+(r.x+x)*4+c;
                    double value = sampled[c]+(operation == NR_SOURCE ? 0 : original[into]*(1-sampled[3]/255));
                    expected[into] = uint8_t(std::lround(std::clamp(value,0.,255.)));
                }
            }
            require(nr_sample_regions(context,&source,&destination,regions,2,points.data(),points.size(),
                        nr_operator(operation),opacity,&stats,error,sizeof(error)) == 0,error);
            require(stats.pixels == count && stats.dispatches == 2, "unexpected sampling batching");
            for (size_t i = 0; i < output.size(); ++i) {
                int difference = std::abs(int(output[i])-int(expected[i]));
                require(difference <= (touched[i] ? 2 : 0),"sampling differs from independent double oracle");
                maximum = std::max(maximum,difference);
            }
        }
        auto before = output;
        points.back().index[0] = W*H;
        require(nr_sample_regions(context,&source,&destination,regions,2,points.data(),points.size(),
                    NR_SOURCE_OVER,1,&stats,error,sizeof(error)) != 0, "invalid last address was accepted");
        require(output == before, "failed sampling partially wrote destination");
        points.back().index[0] = 0;
        require(nr_switch(context,"CPU",error,sizeof(error)) == 0,error);
        require(nr_sample_regions(context,&source,&destination,regions,2,points.data(),points.size(),
                    NR_SOURCE_OVER,1,&stats,error,sizeof(error)) == 0,error);
        require(std::strcmp(stats.execution_devices,"CPU") == 0,"prepared sampler did not follow backend switch");
        nr_destroy(context);
        std::cout << "{\"sampling_passed\":true,\"initial_device\":\"" << device
                  << "\",\"switched_device\":\"CPU\",\"max_byte_error\":" << maximum << "}\n";
        return 0;
    } catch (const std::exception &e) { nr_destroy(context); std::cerr << e.what() << '\n'; return 1; }
}
