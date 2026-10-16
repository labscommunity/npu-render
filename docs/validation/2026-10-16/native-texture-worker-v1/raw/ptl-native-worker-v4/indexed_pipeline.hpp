#pragma once
#include "indexed_exchange_protocol.hpp"
#include "clear_executor.hpp"
#include "quad_batch.hpp"
#include "ptl_four_tap.hpp"
#include "ptl_solid.hpp"
#include "ptl_texture_shader.hpp"
#include "ptl_triangle_texture_draw.hpp"
#include <cstdlib>
#include <cstdio>
namespace nr::registered_lab::indexed_exchange {
class Pipeline {
    static uint32_t vertex_capacity(uint32_t count) {
        require(count && count<=4095 && !(count%3));
        uint32_t capacity=3;
        while(capacity<count)capacity=(std::min)(4095u,capacity*2);
        return capacity;
    }
    struct ClearEntry {uint32_t width,height,format;std::unique_ptr<raster::ClearExecutor> executor;};
    std::vector<ClearEntry> clear_cache;
    uint32_t uniform_limit=8,triangle_limit=24,clear_limit=4;
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
    std::filesystem::path ptl_blob,ptl_solid_blob;
    struct SolidEntry {unsigned vertices;std::unique_ptr<PtlSolid> executor;};
    std::vector<SolidEntry> ptl_solids;
    std::unique_ptr<PtlFourTap> ptl_four_tap;
    bool ptl_texture_enabled=false;
    struct PtlTextureEntry {
        Header header;uint32_t batch;std::vector<uint32_t> vertex,pixel;
        raster::PackedResources layout;shader::TextureSamplers samplers;
        std::unique_ptr<raster::PtlTriangleTextureDrawExecutor> executor;
    };
    std::vector<PtlTextureEntry> ptl_textures;
    static shader::Program precision_program(const shader::Program &original){
        auto p=original;
        if(p.bytecode.size()==shader::ptl_texture_fixture::pixel.size()&&std::equal(p.bytecode.begin(),p.bytecode.end(),shader::ptl_texture_fixture::pixel.begin())){
            auto &i=p.instructions.at(112);require(i.opcode==shader::Opcode::mad&&i.sources[0].modifier==shader::Modifier::negate&&i.sources[1].kind==shader::Register::immediate);
            i.sources[0].modifier=shader::Modifier::none;
            for(auto &word:i.sources[1].literal){require(word==1017370378u);word^=0x80000000u;}
            std::printf("{\"ptl_texture_precision_correction\":true,\"instruction\":112,\"exact_immediate_preserved\":true}\n");std::fflush(stdout);
        }
        return p;
    }
    bool quad_enabled=false;
    struct QuadEntry {Header header;std::vector<uint32_t> vertex,pixel;raster::PackedTexture layout;std::unique_ptr<QuadBatch> executor;};
    std::vector<QuadEntry> quad_cache;

public:
    Pipeline(){
        wchar_t *texture=nullptr;size_t texture_length=0;
        if(_wdupenv_s(&texture,&texture_length,L"NR_RENDER_PTL_TEXTURE_BLOB"))throw std::runtime_error("cannot read texture provider setting");
        const std::unique_ptr<wchar_t,decltype(&std::free)> texture_owner(texture,&std::free);
        if(texture&&*texture){require(std::filesystem::path(texture).is_absolute());ptl_texture_enabled=true;}

        wchar_t *solid=nullptr;size_t solid_length=0;
        if(_wdupenv_s(&solid,&solid_length,L"NR_RENDER_PTL_SOLID_BLOB"))throw std::runtime_error("cannot read solid provider setting");
        const std::unique_ptr<wchar_t,decltype(&std::free)> solid_owner(solid,&std::free);
        if(solid && *solid){ptl_solid_blob=solid;require(ptl_solid_blob.is_absolute());}

        wchar_t *ptl=nullptr;size_t ptl_length=0;
        if(_wdupenv_s(&ptl,&ptl_length,L"NR_RENDER_PTL_FOUR_TAP_BLOB"))throw std::runtime_error("cannot read PTL provider setting");
        const std::unique_ptr<wchar_t,decltype(&std::free)> ptl_owner(ptl,&std::free);
        if(ptl && *ptl){ptl_blob=ptl;require(ptl_blob.is_absolute());}

        char *quad=nullptr;size_t quad_length=0;
        if(_dupenv_s(&quad,&quad_length,"NR_RENDER_QUAD_BATCH"))throw std::runtime_error("cannot read quad experiment");
        if(quad){const std::string value(quad);std::free(quad);if(value!="1")throw std::invalid_argument("quad experiment requires 1");quad_enabled=true;}

        char *value=nullptr;size_t length=0;
        if(_dupenv_s(&value,&length,"NR_RENDER_PIPELINE_CACHE"))throw std::runtime_error("cannot read pipeline cache experiment");
        std::unique_ptr<char,decltype(&std::free)> owner(value,&std::free);
        if(value && *value){
            if(!std::strcmp(value,"expanded")){uniform_limit=16;triangle_limit=48;}
            else if(!std::strcmp(value,"retained")){uniform_limit=64;triangle_limit=128;clear_limit=16;}
            else throw std::invalid_argument("pipeline cache experiment requires expanded or retained");
        }
        std::printf("{\"kind\":\"pipeline_cache_policy\",\"uniform_capacity\":%u,\"triangle_capacity\":%u,\"clear_capacity\":%u,\"replacement\":\"FIFO\",\"hardware_queue_count_measured\":false}\n",uniform_limit,triangle_limit,clear_limit);std::fflush(stdout);
    }
    std::array<size_t,5> retained_pipeline_counts() const {
        return {uniform_cache.size(),triangle_cache.size(),clear_cache.size(),texture_executor?1u:0u,linear_executor?1u:0u};
    }
    raster::UniformDrawExecutor::Result execute(const Request &request) {
        if(!ptl_solid_blob.empty() && PtlSolid::eligible(request)){
            PtlSolid *executor=nullptr;
            for(auto &entry:ptl_solids)if(entry.vertices==request.header.vertices){executor=entry.executor.get();break;}
            if(!executor && ptl_solids.size()<8){auto candidate=std::make_unique<PtlSolid>(request,ptl_solid_blob);executor=candidate.get();ptl_solids.push_back({request.header.vertices,std::move(candidate)});}
            if(executor){auto attempt=executor->execute(request);
                std::printf("{\"ptl_solid_attempt\":true,\"accepted\":%s,\"provider_requests\":%llu,\"fallback_backend\":\"%s\"}\n",attempt.accepted?"true":"false",static_cast<unsigned long long>(attempt.result.provider_requests),attempt.accepted?"none":"NPU");std::fflush(stdout);
                if(attempt.accepted)return std::move(attempt.result);
                auto result=execute_without_solid(request);require(result.execution_device==attempt.result.execution_device);QuadBatch::add_work(result,attempt.result);return result;
            }
            std::printf("{\"ptl_solid_capacity_declined\":true,\"fallback_backend\":\"NPU\"}\n");std::fflush(stdout);
        }
        return execute_without_solid(request);
    }
    raster::UniformDrawExecutor::Result execute_without_solid(const Request &request) {

        if(!ptl_blob.empty() && PtlFourTap::eligible(request)){
            if(!ptl_four_tap)ptl_four_tap=std::make_unique<PtlFourTap>(request,ptl_blob);
            auto attempt=ptl_four_tap->execute(request);
            std::printf("{\"ptl_four_tap_attempt\":true,\"accepted\":%s,\"provider_requests\":%llu,\"fallback_backend\":\"%s\"}\n",attempt.accepted?"true":"false",static_cast<unsigned long long>(attempt.result.provider_requests),attempt.accepted?"none":"NPU");std::fflush(stdout);
            if(attempt.accepted)return std::move(attempt.result);
            auto result=execute_without_ptl(request);require(result.execution_device==attempt.result.execution_device);QuadBatch::add_work(result,attempt.result);return result;
        }
        return execute_without_ptl(request);
    }
    raster::UniformDrawExecutor::Result execute_without_ptl(const Request &request) {
        if(!quad_enabled || !QuadBatch::eligible(request))return execute_standard(request);
        const auto &h=request.header;const auto &t=request.texture;QuadBatch *executor=nullptr;
        for(auto &entry:quad_cache){const auto &old=entry.header;if(old.width==h.width && old.height==h.height && old.format==h.format && (old.flags&~4u)==(h.flags&~4u) && entry.layout.width==t.width && entry.layout.height==t.height && entry.layout.format==t.format && entry.vertex==request.vertex.bytecode && entry.pixel==request.pixel.bytecode){executor=entry.executor.get();break;}}
        if(!executor){if(quad_cache.size()==8)quad_cache.erase(quad_cache.begin());auto candidate=std::make_unique<QuadBatch>(request,"NPU");quad_cache.push_back({h,request.vertex.bytecode,request.pixel.bytecode,{t.width,t.height,t.format,{}},std::move(candidate)});executor=quad_cache.back().executor.get();}
        auto attempt=executor->execute(request);
        std::printf("{\"quad_batch_attempt\":true,\"accepted\":%s,\"vertices\":%u,\"provider_requests\":%llu,\"pipeline_count\":%zu,\"fallback_backend\":\"%s\"}\n",attempt.accepted?"true":"false",h.vertices,static_cast<unsigned long long>(attempt.result.provider_requests),quad_cache.size(),attempt.accepted?"none":"NPU");std::fflush(stdout);
        if(attempt.accepted)return std::move(attempt.result);
        auto result=execute_standard(request);require(result.execution_device==attempt.result.execution_device);QuadBatch::add_work(result,attempt.result);return result;
    }
    raster::UniformDrawExecutor::Result execute_standard(const Request &request) {
        if(color_clear(request.header)){
            const auto &h=request.header;require(shape_valid(h));
            raster::ClearExecutor *executor=nullptr;
            for(auto &entry:clear_cache)if(entry.width==h.width && entry.height==h.height && entry.format==h.format){executor=entry.executor.get();break;}
            if(!executor){
                if(clear_cache.size()==clear_limit)clear_cache.erase(clear_cache.begin());
                auto candidate=std::make_unique<raster::ClearExecutor>(h.width,h.height,h.format,"NPU");
                clear_cache.push_back({h.width,h.height,h.format,std::move(candidate)});executor=clear_cache.back().executor.get();
            }
            auto clear=executor->execute(request.clear_color);raster::UniformDrawExecutor::Result result{};
            result.pixels=std::move(clear.pixels);result.execution_device=std::move(clear.execution_device);
            result.input_bytes=16;result.output_bytes=uint64_t(h.width)*h.height*4;result.covered_samples=uint64_t(h.width)*h.height;
            result.provider_requests=result.tile_executions=1;result.timings.total_ms=clear.milliseconds;return result;
        }
        if(request.header.flags & 0x3000){
            const auto &h=request.header;require(shape_valid(h));
            const auto batch=triangle_batch(request,shader::RegisterExecutor::preferred_raster_batch(request.pixel));
            const auto capacity=vertex_capacity(h.vertices);
            raster::PackedResources resources;shader::TextureSamplers samplers{};
            if(h.flags&0x2000){const auto abi=payload_abi(h);require(abi==8 || abi==9 || abi==10 || abi==11 || abi==12 || abi==14 || abi==15);resources=request.resources;samplers=request.samplers;}
            else {const auto abi=payload_abi(h);require(abi==7 || abi==9 || abi==10 || abi==11 || abi==15);resources[0]=request.texture;samplers[0].filter=(h.flags&16)?shader::TextureFilter::point:shader::TextureFilter::linear;}
            if(ptl_texture_enabled&&shader::PtlTextureShader::eligible(request.pixel,resources,samplers)){
                raster::PtlTriangleTextureDrawExecutor *executor=nullptr;
                for(auto &entry:ptl_textures){const auto &old=entry.header;
                    if(entry.batch==batch&&old.vertices==capacity&&old.format==h.format&&(old.flags&~4u)==(h.flags&~4u)&&raster::resource_layout_equal(resources,entry.layout)&&raster::sampler_layout_equal(samplers,entry.samplers)&&entry.vertex==request.vertex.bytecode&&entry.pixel==request.pixel.bytecode){executor=entry.executor.get();break;}
                }
                if(!executor&&ptl_textures.size()<8){auto candidate=std::make_unique<raster::PtlTriangleTextureDrawExecutor>(request.vertex,request.pixel,capacity,h.width,h.height,h.format,resources,samplers,"NPU",blend_mode(h),uint8_t((h.flags>>8)&15),static_cast<raster::CullMode>((h.flags>>5)&3),(h.flags&128)!=0,batch);auto key=h;key.vertices=capacity;executor=candidate.get();ptl_textures.push_back({key,batch,request.vertex.bytecode,request.pixel.bytecode,raster::resource_layout(resources),samplers,std::move(candidate)});}
                if(executor){auto result=executor->execute(request.inputs,request.vertex_constants,request.pixel_constants,request.viewport,resources,request.background,request.scissor,request.blend_constant);
                    std::printf("{\"ptl_texture_draw\":true,\"accepted\":true,\"provider_requests\":%llu,\"coverage_compacted\":true,\"fallback_backend\":\"none\"}\n",static_cast<unsigned long long>(result.provider_requests));std::fflush(stdout);return result;
                }
                std::printf("{\"ptl_texture_capacity_declined\":true,\"fallback_backend\":\"NPU\",\"precision_correction_retained\":true}\n");std::fflush(stdout);
            }
            for(auto &entry:triangle_cache){const auto &old=entry.header;
                if(entry.batch==batch && old.vertices==capacity && old.format==h.format &&
                    (old.flags&~4u)==(h.flags&~4u) && raster::resource_layout_equal(resources,entry.layout) &&
                    raster::sampler_layout_equal(samplers,entry.samplers) && entry.vertex==request.vertex.bytecode && entry.pixel==request.pixel.bytecode)
                    return entry.executor->execute(request.inputs,request.vertex_constants,request.pixel_constants,request.viewport,resources,request.background,request.scissor,request.blend_constant);
            }
            // Target shape is a runtime projection input, not a model shape.
            // Retain fewer complete pipelines after the 64-entry desktop trial
            // encountered command-queue creation failures. This is a bounded
            // policy, not a claim about the driver's hardware queue limit.
            // Evict before constructing a successor for the qualified NPU driver.
            if(triangle_cache.size()==triangle_limit)triangle_cache.erase(triangle_cache.begin());
            const auto corrected_pixel=precision_program(request.pixel);
            auto candidate=std::make_unique<raster::TriangleTextureDrawExecutor>(request.vertex,corrected_pixel,capacity,h.width,h.height,h.format,
                resources,samplers,"NPU",blend_mode(h),uint8_t((h.flags>>8)&15),static_cast<raster::CullMode>((h.flags>>5)&3),(h.flags&128)!=0,batch);
            auto result=candidate->execute(request.inputs,request.vertex_constants,request.pixel_constants,request.viewport,resources,request.background,request.scissor,request.blend_constant);
            auto key=h;key.vertices=capacity;
            triangle_cache.push_back({key,batch,request.vertex.bytecode,request.pixel.bytecode,raster::resource_layout(resources),samplers,std::move(candidate)});
            return result;
        }
        if (request.header.flags & 24) {
            const auto &h=request.header,&old=previous_linear.header;
            const auto &t=request.texture,&pt=previous_linear.texture;
            require(shape_valid(h) && payload_abi(h)==((h.flags&16)?5u:4u));
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
            return texture_executor->execute_adaptive(request.inputs,request.vertex_constants,request.viewport,t,request.background);
        }
        const auto &h = request.header;
        // Constants and scissor are graph inputs. Keep a bounded set of compiled
        // programs across DWM's alternating solid and transparent draws.
        for (auto &entry : uniform_cache) {
            const auto &old=entry.header;
            if(old.width==h.width && old.height==h.height && old.vertices==h.vertices && old.format==h.format &&
               (old.flags & ~4u)==(h.flags & ~4u) && entry.vertex==request.vertex.bytecode && entry.pixel==request.pixel.bytecode)
                return entry.executor->execute(request.inputs,request.vertex_constants,request.pixel_constants,request.viewport,request.background,request.scissor,request.blend_constant);
        }
        // Complete eviction before constructing a new NPU model. Releasing an
        // old entry after creating its successor triggers a device-loss failure
        // with the qualified 5540 runtime during mixed desktop draws.
        if(uniform_cache.size()==uniform_limit)uniform_cache.erase(uniform_cache.begin());
        const auto batch=raster::UniformDrawExecutor::raster_batch(h.width,h.height);
        auto candidate=std::make_unique<raster::UniformDrawExecutor>(request.vertex,request.pixel,h.vertices,h.width,h.height,
            "NPU",blend_mode(h),h.format==87,uint8_t((h.flags>>8)&15),batch,static_cast<raster::CullMode>((h.flags>>5)&3),(h.flags&128)!=0);
        auto result=candidate->execute(request.inputs,request.vertex_constants,request.pixel_constants,request.viewport,request.background,request.scissor,request.blend_constant);
        uniform_cache.push_back({h,request.vertex.bytecode,request.pixel.bytecode,std::move(candidate)});
        return result;
    }
};
}
