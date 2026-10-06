#include "npu_render.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
int main(int argc, char **argv) {
    try {
        if (argc != 8) throw std::invalid_argument("use DEVICE SOURCE_W SOURCE_H DEST_W DEST_H TILE_SIDE FRAMES");
        const char *device = argv[1];
        int sw=std::stoi(argv[2]), sh=std::stoi(argv[3]), dw=std::stoi(argv[4]), dh=std::stoi(argv[5]);
        int side=std::stoi(argv[6]), frames=std::stoi(argv[7]);
        if (sw<1 || sh<1 || dw<1 || dh<1 || sw>4096 || sh>4096 || dw>4096 || dh>4096 ||
            side<1 || side>1024 || frames<1 || frames>1000) throw std::invalid_argument("invalid extent or frame count");
        std::vector<uint8_t> src(size_t(sw)*sh*4), dst(size_t(dw)*dh*4), original(dst.size());
        for (size_t i=0;i<original.size();++i) original[i]=i%4==3?255:uint8_t((i*7)%256);
        std::vector<nr_sample_point> coordinates(size_t(dw)*dh);
        const auto coordinate_start=Clock::now();
        for (int y=0;y<dh;++y) for (int x=0;x<dw;++x) {
            double sx=(x+.5)*sw/dw-.5, sy=(y+.5)*sh/dh-.5;
            int ix=int(std::floor(sx)), iy=int(std::floor(sy));
            double fx=sx-ix, fy=sy-iy;
            auto &point=coordinates[size_t(y)*dw+x];
            for (int k=0;k<4;++k) {
                int tx=std::clamp(ix+(k&1),0,sw-1), ty=std::clamp(iy+(k>>1),0,sh-1);
                point.index[k]=uint32_t(ty*sw+tx);
                point.weight[k]=float(((k&1)?fx:1-fx)*((k>>1)?fy:1-fy));
            }
        }
        const double coordinate_ms=std::chrono::duration<double,std::milli>(Clock::now()-coordinate_start).count();
        char error[2048];
        const auto preparation_start=Clock::now();
        std::unique_ptr<nr_context,decltype(&nr_destroy)> context(nr_create(device,uint32_t(side),error,sizeof(error)),nr_destroy);
        if (!context) throw std::runtime_error(error);
        if (nr_prepare_sampling(context.get(),sw,sh,error,sizeof(error))) throw std::runtime_error(error);
        const double preparation_ms=std::chrono::duration<double,std::milli>(Clock::now()-preparation_start).count();
        nr_surface source{src.data(),src.size(),size_t(sw)*4,uint32_t(sw),uint32_t(sh),0};
        nr_surface destination{dst.data(),dst.size(),size_t(dw)*4,uint32_t(dw),uint32_t(dh),0};
        nr_damage_rect region{0,0,uint32_t(dw),uint32_t(dh)};
        nr_frame_stats stats{};
        std::vector<double> times,fixture_times;
        int max_error=0;
        for (int frame=0;frame<frames;++frame) {
            const auto fixture_start=Clock::now();
            for (size_t i=0;i<src.size();++i) src[i]=i%4==3?160:uint8_t((i*13+frame*19)%161);
            std::copy(original.begin(),original.end(),dst.begin());
            if (nr_sample_regions(context.get(),&source,&destination,&region,1,coordinates.data(),coordinates.size(),
                                  NR_SOURCE_OVER,1,&stats,error,sizeof(error))) throw std::runtime_error(error);
            times.push_back(stats.elapsed_ms);
            for (size_t i=0;i<coordinates.size();++i) {
                double value[4]{};
                for (size_t k=0;k<4;++k) for (size_t c=0;c<4;++c)
                    value[c]+=src[size_t(coordinates[i].index[k])*4+c]*double(coordinates[i].weight[k]);
                for (size_t c=0;c<4;++c) {
                    int expected=int(std::lround(std::clamp(value[c]+original[i*4+c]*(1-value[3]/255),0.,255.)));
                    int difference=std::abs(int(dst[i*4+c])-expected);
                    max_error=std::max(max_error,difference);
                    if (difference>2) throw std::runtime_error("sampling benchmark failed independent pixel oracle");
                }
            }
            fixture_times.push_back(std::chrono::duration<double,std::milli>(Clock::now()-fixture_start).count());
        }
        std::cout << "{\"passed\":true,\"device\":\"" << device << "\",\"execution_devices\":\"" << stats.execution_devices
                  << "\",\"source\":[" << sw << ',' << sh << "],\"destination\":[" << dw << ',' << dh
                  << "],\"tile_side\":" << side << ",\"frames\":" << frames << ",\"coordinate_cpu_ms\":" << coordinate_ms
                  << ",\"preparation_ms\":" << preparation_ms << ",\"max_byte_error\":" << max_error
                  << ",\"dispatches_per_frame\":" << stats.dispatches << ",\"api_ms\":[";
        for (size_t i=0;i<times.size();++i) std::cout << (i?",":"") << times[i];
        std::cout << "],\"fixture_update_call_check_ms\":[";
        for (size_t i=0;i<fixture_times.size();++i) std::cout << (i?",":"") << fixture_times[i];
        std::cout << "],\"scope\":\"host-source texture sampling; static CPU-generated coordinates; no presentation\"}\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
