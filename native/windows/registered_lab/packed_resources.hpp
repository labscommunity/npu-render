#pragma once
#include "point_texture_draw.hpp"
#include <cstring>
namespace nr::raster {
// Normalized transport formats: RGBA/BGRA UNORM texels or R32_UINT buffer
// elements. Narrow integer views are zero-extended at the graphics boundary.
using PackedResources=std::array<PackedTexture,16>;
inline shader::TextureShapes resource_shapes(const shader::Program &program,const PackedResources &resources){
    shader::TextureShapes shapes{};
    for(unsigned slot=0;slot<16;++slot){const auto &r=resources[slot];
        if(!r.width && !r.height && !r.format){shader::check(r.pixels.empty());continue;}
        shader::check(program.textures[slot] && r.width && r.height && uint64_t(r.width)*r.height<=4194304);
        const bool buffer=program.texture_dimensions[slot]==1;
        shader::check(buffer?(program.texture_uint[slot] && r.format==42 && r.height==1):
            (!program.texture_uint[slot] && (r.format==28 || r.format==87) && r.width<=8192 && r.height<=8192));
        shapes[slot]={r.width,r.height,buffer?1u:15u};
    }
    return shapes;
}
inline shader::TexturePixels decode_resources(const shader::Program &program,const PackedResources &resources){
    const auto shapes=resource_shapes(program,resources);shader::TexturePixels result;
    for(unsigned slot=0;slot<16;++slot){const auto &r=resources[slot];auto &out=result[slot];
        shader::check(r.pixels.size()==size_t(shapes[slot].width)*shapes[slot].height);out.resize(r.pixels.size());
        for(size_t n=0;n<out.size();++n){
            if(r.format==42){std::memcpy(&out[n][0],r.pixels[n].data(),4);const uint32_t one=1;std::memcpy(&out[n][3],&one,4);}
            else for(unsigned c=0;c<4;++c)out[n][c]=float(r.pixels[n][r.format==87 && c<3?2-c:c])/255;
        }
    }
    return result;
}
inline bool resource_layout_equal(const PackedResources &a,const PackedResources &b){
    for(unsigned i=0;i<16;++i)if(a[i].width!=b[i].width || a[i].height!=b[i].height || a[i].format!=b[i].format)return false;
    return true;
}
inline PackedResources resource_layout(const PackedResources &a){
    PackedResources result{};for(unsigned i=0;i<16;++i)result[i]={a[i].width,a[i].height,a[i].format,{}};return result;
}
inline bool sampler_layout_equal(const shader::TextureSamplers &a,const shader::TextureSamplers &b){
    for(unsigned i=0;i<16;++i)if(a[i].filter!=b[i].filter || a[i].u!=b[i].u || a[i].v!=b[i].v)return false;return true;
}
}
