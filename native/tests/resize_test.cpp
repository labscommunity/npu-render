#include "npu_render.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

static void require(bool passed, const char *message) {
    if (!passed) throw std::runtime_error(message);
}

// Independent byte-domain oracle: no OpenVINO tensors or production coordinates.
static std::vector<uint8_t> expected(const nr_surface &source, const nr_surface &destination,
                                     uint32_t rw, uint32_t rh, int ox, int oy,
                                     const std::vector<nr_damage_rect> &damage,
                                     nr_operator operation, double opacity) {
    std::vector<uint8_t> result(destination.data, destination.data+destination.length);
    for (const auto &r : damage) for (uint32_t y=r.y; y<r.y+r.height; ++y)
        for (uint32_t x=r.x; x<r.x+r.width; ++x) {
            const double sx=(int64_t(x)-ox+.5)*source.width/rw-.5;
            const double sy=(int64_t(y)-oy+.5)*source.height/rh-.5;
            const int ix=int(std::floor(sx)), iy=int(std::floor(sy));
            const double fx=sx-ix, fy=sy-iy;
            double sample[4]{};
            for (int dy=0; dy<2; ++dy) for (int dx=0; dx<2; ++dx) {
                const int tx=std::clamp(ix+dx,0,int(source.width)-1);
                const int ty=std::clamp(iy+dy,0,int(source.height)-1);
                for (int c=0; c<4; ++c)
                    sample[c]+=(c==3 && source.opaque?255:source.data[ty*source.stride+tx*4+c]) *
                               (dx?fx:1-fx)*(dy?fy:1-fy)*opacity;
            }
            for (int c=0; c<4; ++c) {
                const size_t index=y*destination.stride+x*4+c;
                const int bg=operation==NR_SOURCE?0:c==3 && destination.opaque?255:destination.data[index];
                result[index]=c==3 && destination.opaque?255:uint8_t(std::lround(
                    std::clamp(sample[c]+bg*(1-sample[3]/255),0.,255.)));
            }
        }
    return result;
}

static int compare(const nr_surface &destination, const std::vector<uint8_t> &reference,
                   const std::vector<nr_damage_rect> &damage) {
    int maximum=0;
    for (size_t i=0; i<reference.size(); ++i) {
        const size_t y=i/destination.stride, x=(i%destination.stride)/4;
        bool touched=false;
        for (const auto &r:damage) touched |= x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height;
        const int difference=std::abs(int(reference[i])-destination.data[i]);
        require(difference<=(touched?2:0),"resize differs from independent oracle or modified undamaged bytes");
        maximum=std::max(maximum,difference);
    }
    return maximum;
}

int main(int argc, char **argv) {
    try {
        require(argc==2,"provide CPU, GPU or NPU");
        char error[4096];
        std::unique_ptr<nr_context,decltype(&nr_destroy)> ctx(nr_create(argv[1],32,error,sizeof(error)),nr_destroy);
        require(bool(ctx),error);
        constexpr uint32_t sw=33, sh=19, dw=31, dh=25;
        const size_t ss=sw*4+12, ds=dw*4+20;
        std::vector<uint8_t> src(ss*sh,0xdd), original(ds*dh,0xcc), dst;
        for (uint32_t y=0;y<sh;++y) for (uint32_t x=0;x<sw;++x) {
            const unsigned a=(y*sw+x)*17%256;
            for (unsigned c=0;c<4;++c) src[y*ss+x*4+c]=c==3?a:(x*31+y*13+c*9)%(a+1);
        }
        for (uint32_t y=0;y<dh;++y) for (uint32_t x=0;x<dw;++x)
            for (unsigned c=0;c<4;++c) original[y*ds+x*4+c]=c==3?190:(x*19+y*7+c*31)%191;
        dst=original;
        nr_surface source{src.data(),src.size(),ss,sw,sh,0};
        nr_surface destination{dst.data(),dst.size(),ds,dw,dh,0};
        const std::vector<nr_damage_rect> damage{{3,2,13,7},{19,12,9,10}};
        nr_frame_stats stats{};
        auto call=[&](uint32_t rw,uint32_t rh,int ox,int oy,nr_operator op,float opacity) {
            return nr_resize_regions(ctx.get(),&source,&destination,rw,rh,ox,oy,damage.data(),damage.size(),
                                     op,opacity,&stats,error,sizeof(error));
        };
        require(call(66,38,-11,-6,NR_SOURCE_OVER,1)!=0 && dst==original,"unprepared resize changed destination");
        int maximum=0, cases=0;
        for (const auto &extent:std::vector<std::pair<uint32_t,uint32_t>>{{66,38},{57,43},{29,23}}) {
            require(nr_prepare_resize(ctx.get(),sw,sh,extent.first,extent.second,error,sizeof(error))==0,error);
            const int ox=extent.first==29?0:-11, oy=extent.first==29?0:-6;
            for (int opaque=0;opaque<4;++opaque) for (int op=0;op<2;++op) for (float opacity:{0.f,.5f,1.f}) {
                source.opaque=opaque&1; destination.opaque=opaque&2;
                std::copy(original.begin(),original.end(),dst.begin());
                const auto reference=expected(source,destination,extent.first,extent.second,ox,oy,damage,nr_operator(op),opacity);
                require(call(extent.first,extent.second,ox,oy,nr_operator(op),opacity)==0,error);
                require(stats.pixels==181 && stats.dispatches==1,"incorrect resize work accounting");
                require(std::strncmp(stats.execution_devices,argv[1],3)==0,"resize did not use requested device");
                maximum=std::max(maximum,compare(destination,reference,damage)); ++cases;
            }
        }
        std::copy(original.begin(),original.end(),dst.begin());
        require(call(66,38,4,0,NR_SOURCE_OVER,1)!=0 && dst==original,"invalid virtual bounds committed pixels");
        const nr_damage_rect late_invalid[]={{3,2,13,7},{30,24,2,2}};
        require(nr_resize_regions(ctx.get(),&source,&destination,66,38,0,0,late_invalid,2,NR_SOURCE_OVER,1,
                                 &stats,error,sizeof(error))!=0 && dst==original,"late invalid rectangle committed pixels");
        const nr_damage_rect overlapping[]={{3,2,13,7},{5,4,7,3}};
        require(nr_resize_regions(ctx.get(),&source,&destination,66,38,0,0,overlapping,2,NR_SOURCE_OVER,1,
                                 &stats,error,sizeof(error))!=0 && dst==original,"overlapping rectangles committed pixels");
        require(call(66,38,0,0,NR_SOURCE_OVER,std::numeric_limits<float>::quiet_NaN())!=0 && dst==original,
                "invalid opacity committed pixels");
        require(nr_prepare_resize(ctx.get(),sw,sh,0,38,error,sizeof(error))!=0,"invalid extent prepared");
        require(nr_switch(ctx.get(),"AUTO",error,sizeof(error))!=0,"unsupported backend accepted");
        require(call(66,38,0,0,NR_SOURCE_OVER,1)==0,error);
        require(std::strncmp(stats.execution_devices,argv[1],3)==0,"failed switch changed backend");
        // One source aliases the destination, so packing/commit order matters.
        require(nr_prepare_resize(ctx.get(),dw,dh,dw,dh,error,sizeof(error))==0,error);
        destination.opaque=0;
        const auto alias_reference=expected(destination,destination,dw,dh,0,0,damage,NR_SOURCE_OVER,.5);
        require(nr_resize_regions(ctx.get(),&destination,&destination,dw,dh,0,0,damage.data(),damage.size(),
                                 NR_SOURCE_OVER,.5f,&stats,error,sizeof(error))==0,error);
        maximum=std::max(maximum,compare(destination,alias_reference,damage)); ++cases;
        require(nr_switch(ctx.get(),"CPU",error,sizeof(error))==0,error);
        require(call(66,38,0,0,NR_SOURCE_OVER,1)==0,error);
        require(std::strncmp(stats.execution_devices,"CPU",3)==0,"switch did not prepare retained resize graphs");
        std::cout << "{\"resize_oracle\":true,\"device\":\"" << argv[1] << "\",\"cases\":" << cases
                  << ",\"max_byte_error\":" << maximum << ",\"rollback_alias_switch\":true}\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
