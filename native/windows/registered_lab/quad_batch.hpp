#pragma once
#include "indexed_exchange_protocol.hpp"
#include <chrono>
namespace nr::registered_lab::indexed_exchange {
// Ordered point-sampled quads reuse the already qualified affine renderer.
// Every quad is checked on the backend before any raster result is generated.
// Unsupported geometry returns its work counters to the ordinary NPU path;
// provider failures propagate and never become an implicit fallback.
class QuadBatch {
    raster::LinearTextureDrawExecutor executor;
public:
    struct Attempt {bool accepted=false;raster::UniformDrawExecutor::Result result;};
    static bool eligible(const Request &r){const auto &h=r.header;
        if((h.flags&~5u)!=0x1f12 || !h.vertices || h.vertices%6 || h.vertices>4092 || h.width>4096 || r.viewport.x!=0 || r.viewport.y!=0 || r.viewport.width!=float(h.width) || r.viewport.height!=float(h.height))return false;
        try{raster::LinearTextureDrawExecutor::validate_shader(r.pixel);}catch(const std::invalid_argument &){return false;}return true;
    }
    static void add_work(raster::UniformDrawExecutor::Result &to,const raster::UniformDrawExecutor::Result &from){
        to.input_bytes+=from.input_bytes;to.output_bytes+=from.output_bytes;to.provider_requests+=from.provider_requests;to.covered_samples+=from.covered_samples;to.tile_executions+=from.tile_executions;
        to.timings.shader_ms+=from.timings.shader_ms;to.timings.projection_ms+=from.timings.projection_ms;to.timings.coverage_ms+=from.timings.coverage_ms;to.timings.merger_ms+=from.timings.merger_ms;to.timings.total_ms+=from.timings.total_ms;
    }
    QuadBatch(const Request &r,const std::string &backend):executor(r.vertex,r.pixel,r.header.width,r.header.height,r.header.format,r.texture.width,r.texture.height,r.texture.format,backend,(r.header.flags&1)!=0,shader::TextureFilter::point){require(eligible(r));}
    Attempt execute(const Request &r){
        require(eligible(r));const auto start=std::chrono::steady_clock::now();Attempt attempt;auto &result=attempt.result;
        for(size_t first=0;first<r.inputs.size();first+=6){std::vector<shader::VertexRegisters> vertices(r.inputs.begin()+first,r.inputs.begin()+first+6);const auto proof=executor.preflight_quad(vertices,r.vertex_constants,r.viewport);
            if(result.execution_device.empty())result.execution_device=proof.execution_device;require(result.execution_device==proof.execution_device);add_work(result,proof);
            if(!proof.used_rectangle){result.timings.total_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return attempt;}}
        result.pixels=r.background;
        for(size_t first=0;first<r.inputs.size();first+=6){std::vector<shader::VertexRegisters> vertices(r.inputs.begin()+first,r.inputs.begin()+first+6);auto quad=executor.execute(vertices,r.vertex_constants,r.viewport,r.texture,result.pixels,r.scissor);
            require(quad.execution_device==result.execution_device && quad.used_rectangle);add_work(result,quad);result.vertices.insert(result.vertices.end(),quad.vertices.begin(),quad.vertices.end());result.pixels=std::move(quad.pixels);}
        attempt.accepted=true;result.used_rectangle=true;result.timings.total_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return attempt;
    }
};
}
