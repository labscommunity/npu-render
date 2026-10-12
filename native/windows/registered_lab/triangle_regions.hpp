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
    const std::vector<std::array<int32_t,4>> &bounds,const std::array<int32_t,4> &clip,uint32_t batch,bool quads=false){
    if(!batch || batch>8192 || (quads && batch%4) || clip[0]<0 || clip[1]<0 || clip[0]>clip[2] || clip[1]>clip[3] || clip[2]>8192 || clip[3]>8192)
        throw std::invalid_argument("invalid triangle region parameters");
    std::vector<std::array<int32_t,4>> regions;regions.reserve(bounds.size());uint64_t tiles=0;
    for(const auto &b:bounds){
        if(b[0]<0 || b[1]<0 || b[0]>b[2] || b[1]>b[3] || b[2]>8192 || b[3]>8192)
            throw std::invalid_argument("invalid device triangle bounds");
        std::array<int32_t,4> r{(std::max)(clip[0],b[0]),(std::max)(clip[1],b[1]),(std::min)(clip[2],b[2]),(std::min)(clip[3],b[3])};
        if(quads && r[0]<r[2] && r[1]<r[3]){r[0]&=~1;r[1]&=~1;r[2]=(r[2]+1)&~1;r[3]=(r[3]+1)&~1;}
        if(r[0]<r[2] && r[1]<r[3])tiles+=(uint64_t(r[2]-r[0])*uint64_t(r[3]-r[1])+batch-1)/batch;
        if(tiles>4096)throw std::invalid_argument("triangle raster budget exceeded");
        regions.push_back(r);
    }
    return regions;
}
// A later overlapping region must execute after every earlier one. Regions
// in the same layer are disjoint, so their fragments can share an NPU batch
// without changing blend order. Bounds include any derivative helper quads.
inline std::vector<std::vector<size_t>> triangle_region_layers(const std::vector<std::array<int32_t,4>> &regions){
    if(regions.size()>4096/3)throw std::invalid_argument("too many triangle regions");
    std::vector<size_t> depth(regions.size());std::vector<std::vector<size_t>> layers;
    for(size_t i=0;i<regions.size();++i){
        const auto &a=regions[i];if(a[0]>=a[2] || a[1]>=a[3])continue;
        for(size_t j=0;j<i;++j){const auto &b=regions[j];
            if(b[0]<b[2] && b[1]<b[3] && a[0]<b[2] && b[0]<a[2] && a[1]<b[3] && b[1]<a[3])
                depth[i]=(std::max)(depth[i],depth[j]+1);
        }
        if(layers.size()<=depth[i])layers.resize(depth[i]+1);
        layers[depth[i]].push_back(i);
    }
    return layers;
}
}
