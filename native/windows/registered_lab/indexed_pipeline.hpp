#pragma once
#include "indexed_exchange_protocol.hpp"
namespace nr::registered_lab::indexed_exchange {
class Pipeline {
    struct UniformEntry {
        Header header;
        std::vector<uint32_t> vertex, pixel;
        std::unique_ptr<raster::UniformDrawExecutor> executor;
    };
    std::vector<UniformEntry> uniform_cache;
    std::unique_ptr<raster::PointTextureDrawExecutor> texture_executor;
    Request previous_texture;
    std::unique_ptr<raster::LinearTextureDrawExecutor> linear_executor;
    Request previous_linear;
    struct TriangleEntry {
        Header header;
        uint32_t batch;
        std::vector<uint32_t> vertex,pixel;
        raster::PackedResources layout;
        shader::TextureSamplers samplers;
        std::unique_ptr<raster::TriangleTextureDrawExecutor> executor;
    };
    std::vector<TriangleEntry> triangle_cache;
public:
    raster::UniformDrawExecutor::Result execute(const Request &request) {
        if(request.header.flags & 0x3000){
            const auto &h=request.header;require(shape_valid(h));
            const auto batch=triangle_batch(request);
            raster::PackedResources resources;shader::TextureSamplers samplers{};
            if(h.flags&0x2000){require(h.abi==8 || h.abi==9 || h.abi==10);resources=request.resources;samplers=request.samplers;}
            else {require(h.abi==7 || h.abi==9 || h.abi==10);resources[0]=request.texture;samplers[0].filter=(h.flags&16)?shader::TextureFilter::point:shader::TextureFilter::linear;}
            for(auto &entry:triangle_cache){const auto &old=entry.header;
                if(entry.batch==batch && old.width==h.width && old.height==h.height && old.vertices==h.vertices && old.format==h.format &&
                    (old.flags&~4u)==(h.flags&~4u) && raster::resource_layout_equal(resources,entry.layout) &&
                    raster::sampler_layout_equal(samplers,entry.samplers) && entry.vertex==request.vertex.bytecode && entry.pixel==request.pixel.bytecode)
                    return entry.executor->execute(request.inputs,request.vertex_constants,request.pixel_constants,request.viewport,resources,request.background,request.scissor);
            }
            // Alternating desktop draw sizes must not recompile each frame.
            // Evict before constructing a successor for the qualified NPU driver.
            if(triangle_cache.size()==4)triangle_cache.erase(triangle_cache.begin());
            auto candidate=std::make_unique<raster::TriangleTextureDrawExecutor>(request.vertex,request.pixel,h.vertices,h.width,h.height,h.format,
                resources,samplers,"NPU",blend_mode(h),uint8_t((h.flags>>8)&15),static_cast<raster::CullMode>((h.flags>>5)&3),(h.flags&128)!=0,batch);
            auto result=candidate->execute(request.inputs,request.vertex_constants,request.pixel_constants,request.viewport,resources,request.background,request.scissor);
            triangle_cache.push_back({h,batch,request.vertex.bytecode,request.pixel.bytecode,raster::resource_layout(resources),samplers,std::move(candidate)});
            return result;
        }
        if (request.header.flags & 24) {
            const auto &h=request.header,&old=previous_linear.header;
            const auto &t=request.texture,&pt=previous_linear.texture;
            require(shape_valid(h) && h.abi==((h.flags&16)?5u:4u));
            if(!linear_executor || old.width!=h.width || old.height!=h.height || old.format!=h.format ||
                (old.flags & ~4u)!=(h.flags & ~4u) || pt.width!=t.width || pt.height!=t.height || pt.format!=t.format ||
                previous_linear.vertex.bytecode!=request.vertex.bytecode || previous_linear.pixel.bytecode!=request.pixel.bytecode){
                linear_executor.reset();
                auto candidate=std::make_unique<raster::LinearTextureDrawExecutor>(request.vertex,request.pixel,h.width,h.height,h.format,t.width,t.height,t.format,"NPU",(h.flags&1)!=0,(h.flags&16)?shader::TextureFilter::point:shader::TextureFilter::linear);
                linear_executor=std::move(candidate);previous_linear={};previous_linear.header=h;
                previous_linear.vertex=request.vertex;previous_linear.pixel=request.pixel;previous_linear.texture={t.width,t.height,t.format,{}};
            }
            return linear_executor->execute(request.inputs,request.vertex_constants,request.viewport,t,request.background,request.scissor);
        }
        if (request.header.flags & 2) {
            const auto &h = request.header, &old = previous_texture.header;
            const auto &t = request.texture, &pt = previous_texture.texture;
            require(h.vertices == 6 && h.flags == 0xf02 && h.abi == 2);
            if (!texture_executor || old.width != h.width || old.height != h.height || old.format != h.format ||
                pt.width != t.width || pt.height != t.height || pt.format != t.format ||
                previous_texture.vertex.bytecode != request.vertex.bytecode || previous_texture.pixel.bytecode != request.pixel.bytecode) {
                texture_executor.reset();
                auto candidate = std::make_unique<raster::PointTextureDrawExecutor>(request.vertex,request.pixel,h.width,h.height,h.format,t.width,t.height,t.format,"NPU");
                texture_executor = std::move(candidate);
                previous_texture = {}; previous_texture.header = h; previous_texture.vertex = request.vertex; previous_texture.pixel = request.pixel;
                previous_texture.texture = {t.width,t.height,t.format,{}};
            }
            return texture_executor->execute(request.inputs,request.vertex_constants,request.viewport,t);
        }
        const auto &h = request.header;
        // Constants and scissor are graph inputs. Keep a bounded set of compiled
        // programs across DWM's alternating solid and transparent draws.
        for (auto &entry : uniform_cache) {
            const auto &old=entry.header;
            if(old.width==h.width && old.height==h.height && old.vertices==h.vertices && old.format==h.format &&
               (old.flags & ~4u)==(h.flags & ~4u) && entry.vertex==request.vertex.bytecode && entry.pixel==request.pixel.bytecode)
                return entry.executor->execute(request.inputs,request.vertex_constants,request.pixel_constants,request.viewport,request.background,request.scissor);
        }
        // Complete eviction before constructing a new NPU model. Releasing an
        // old entry after creating its successor triggers a device-loss failure
        // with the qualified 5540 runtime during mixed desktop draws.
        if(uniform_cache.size()==8)uniform_cache.erase(uniform_cache.begin());
        uint32_t batch=4096;while(uint64_t(h.width)*h.height%batch)batch/=2;
        auto candidate=std::make_unique<raster::UniformDrawExecutor>(request.vertex,request.pixel,h.vertices,h.width,h.height,
            "NPU",blend_mode(h),h.format==87,uint8_t((h.flags>>8)&15),batch,static_cast<raster::CullMode>((h.flags>>5)&3),(h.flags&128)!=0);
        auto result=candidate->execute(request.inputs,request.vertex_constants,request.pixel_constants,request.viewport,request.background,request.scissor);
        uniform_cache.push_back({h,request.vertex.bytecode,request.pixel.bytecode,std::move(candidate)});
        return result;
    }
};
}
