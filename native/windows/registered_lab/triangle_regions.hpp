#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace nr::raster {
// Address scheduling over bounds already computed by the selected device.
// This does not evaluate vertex shaders, triangle edges or sample coverage.
inline std::vector<std::array<int32_t,4>> bounded_triangle_regions(
    const std::vector<std::array<int32_t,4>> &bounds,const std::array<int32_t,4> &clip,uint32_t batch){
    if(!batch || batch>4096 || clip[0]<0 || clip[1]<0 || clip[0]>clip[2] || clip[1]>clip[3] || clip[2]>8192 || clip[3]>8192)
        throw std::invalid_argument("invalid triangle region parameters");
    std::vector<std::array<int32_t,4>> regions;regions.reserve(bounds.size());uint64_t tiles=0;
    for(const auto &b:bounds){
        if(b[0]<0 || b[1]<0 || b[0]>b[2] || b[1]>b[3] || b[2]>8192 || b[3]>8192)
            throw std::invalid_argument("invalid device triangle bounds");
        const std::array<int32_t,4> r{(std::max)(clip[0],b[0]),(std::max)(clip[1],b[1]),(std::min)(clip[2],b[2]),(std::min)(clip[3],b[3])};
        if(r[0]<r[2] && r[1]<r[3])tiles+=(uint64_t(r[2]-r[0])*uint64_t(r[3]-r[1])+batch-1)/batch;
        if(tiles>4096)throw std::invalid_argument("triangle raster budget exceeded");
        regions.push_back(r);
    }
    return regions;
}
}
