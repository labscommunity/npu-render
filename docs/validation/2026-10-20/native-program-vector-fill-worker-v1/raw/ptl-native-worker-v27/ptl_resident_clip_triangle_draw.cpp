#include "ptl_resident_clip_triangle_draw.hpp"
#include "triangle_regions.hpp"
#include "ptl_resident_clip.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <numeric>
namespace nr::raster {
namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void require(bool v){if(!v)throw std::invalid_argument("unsupported textured triangle draw");}
}
void PtlResidentClipTriangleDrawExecutor::validate_shader(const shader::Program &p){
    require(p.stage==shader::Stage::pixel && p.outputs[0]==15);
    for(unsigned r=0;r<16;++r){
        if(r)require(!p.outputs[r]);
        if(p.inputs[r] && r!=p.position_input)require(p.interpolation[r]==1 || p.interpolation[r]==2 || p.interpolation[r]==4);
    }
    for(const auto &i:p.instructions)for(unsigned a=0;a<i.source_count;++a)
        require(i.sources[a].kind!=shader::Register::input || i.sources[a].index!=p.position_input);
    shader::RegisterExecutor::validate(p);
}
struct PtlResidentClipTriangleDrawExecutor::State {
    shader::Program vs,ps;
    uint32_t count,width,height,format,batch;
    bool quads;
    PackedResources layout;
    shader::VertexExecutor vertex;
    ProjectionExecutor projection;
    PtlResidentClip program;
    State(const shader::Program &v,const shader::Program &p,uint32_t n,uint32_t w,uint32_t h,uint32_t f,
        const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,
        BlendMode blend,uint8_t mask,CullMode cull,bool ccw,uint32_t b)
        :vs(v),ps(p),count(n),width(w),height(h),format(f),batch(b),quads(shader::quad_derivatives(p)),layout(resource_layout(resources)),
         vertex(v,n,backend),projection(n,backend),program(p,resources,samplers,cull,ccw,f,blend,mask,backend){
        validate_shader(ps);
        require(n && n<=4096 && !(n%3) && w && w<=8192 && h && h<=8192 && uint64_t(w)*h<=8388576 &&
            b && b<=PtlResidentClip::maximum_batch(resources,n) && (!quads || !(b%4)) &&
            (f==28 || f==87) && vs.stage==shader::Stage::vertex && vs.position_output<16);
        require(!quads&&n<=2046);
        for(unsigned r=0;r<16;++r)if(ps.inputs[r]&&r!=ps.position_input)require((vs.outputs[r]&ps.inputs[r])==ps.inputs[r]);
    }
};
PtlResidentClipTriangleDrawExecutor::PtlResidentClipTriangleDrawExecutor(const shader::Program &v,const shader::Program &p,uint32_t n,uint32_t w,uint32_t h,uint32_t f,
    uint32_t tw,uint32_t th,uint32_t tf,const std::string &backend,
    bool blend,uint8_t mask,shader::TextureFilter filter,CullMode cull,bool ccw,uint32_t batch)
    :PtlResidentClipTriangleDrawExecutor(v,p,n,w,h,f,tw,th,tf,backend,source_blend(blend),mask,filter,cull,ccw,batch){}
PtlResidentClipTriangleDrawExecutor::PtlResidentClipTriangleDrawExecutor(const shader::Program &v,const shader::Program &p,uint32_t n,uint32_t w,uint32_t h,uint32_t f,
    const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,
    bool blend,uint8_t mask,CullMode cull,bool ccw,uint32_t batch)
    :PtlResidentClipTriangleDrawExecutor(v,p,n,w,h,f,resources,samplers,backend,source_blend(blend),mask,cull,ccw,batch){}
PtlResidentClipTriangleDrawExecutor::PtlResidentClipTriangleDrawExecutor(const shader::Program &v,const shader::Program &p,
    uint32_t n,uint32_t w,uint32_t h,uint32_t f,uint32_t sw,uint32_t sh,uint32_t sf,const std::string &backend,
    BlendMode blend,uint8_t mask,shader::TextureFilter filter,CullMode cull,bool ccw,uint32_t batch)
    :state(nullptr){
    PackedResources resources{};resources[0]={sw,sh,sf,{}};shader::TextureSamplers samplers{};samplers[0].filter=filter;
    state=std::make_unique<State>(v,p,n,w,h,f,resources,samplers,backend,blend,mask,cull,ccw,batch);
}
PtlResidentClipTriangleDrawExecutor::PtlResidentClipTriangleDrawExecutor(const shader::Program &v,const shader::Program &p,
    uint32_t n,uint32_t w,uint32_t h,uint32_t f,const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,
    BlendMode blend,uint8_t mask,CullMode cull,bool ccw,uint32_t batch)
    :state(std::make_unique<State>(v,p,n,w,h,f,resources,samplers,backend,blend,mask,cull,ccw,batch)){}
PtlResidentClipTriangleDrawExecutor::~PtlResidentClipTriangleDrawExecutor()=default;
UniformDrawExecutor::Result PtlResidentClipTriangleDrawExecutor::execute(const std::vector<shader::VertexRegisters> &inputs,
    const shader::VertexConstants &vc,const shader::VertexConstants &pc,const Viewport &viewport,const PackedTexture &source,
    const std::vector<PackedColor> &background,const std::optional<std::array<int32_t,4>> &scissor,const Color &blend_constant){
    PackedResources resources{};resources[0]=source;return execute(inputs,vc,pc,viewport,resources,background,scissor,blend_constant);
}
UniformDrawExecutor::Result PtlResidentClipTriangleDrawExecutor::execute(const std::vector<shader::VertexRegisters> &inputs,
    const shader::VertexConstants &vc,const shader::VertexConstants &pc,const Viewport &viewport,const PackedResources &resources,
    const std::vector<PackedColor> &background,const std::optional<std::array<int32_t,4>> &scissor,const Color &blend_constant){
    (void)pc;(void)blend_constant;auto &s=*state;const auto start=Clock::now();
    // Projection accepts viewport dimensions as provider inputs; coverage and
    // merger models operate on bounded batches, independent of target shape.
    require(viewport.x==0 && viewport.y==0 && std::isfinite(viewport.width) && std::isfinite(viewport.height) &&
        viewport.width>=1 && viewport.width<=8192 && viewport.height>=1 && viewport.height<=8192);
    const auto target_width=uint32_t(viewport.width),target_height=uint32_t(viewport.height);
    require(viewport.width==float(target_width) && viewport.height==float(target_height) &&
        uint64_t(target_width)*target_height<=8388576 && !inputs.empty() && inputs.size()<=s.count && inputs.size()<=2046 && !(inputs.size()%3) &&
        background.size()==size_t(target_width)*target_height && resource_layout_equal(resources,s.layout));
    const auto clip=scissor.value_or(std::array<int32_t,4>{0,0,int32_t(target_width),int32_t(target_height)});
    require(clip[0]>=0 && clip[1]>=0 && clip[0]<=clip[2] && clip[1]<=clip[3] && clip[2]<=int32_t(target_width) && clip[3]<=int32_t(target_height));
    const std::array<int32_t,4> fixed_clip{clip[0]*256,clip[1]*256,clip[2]*256,clip[3]*256};
    UniformDrawExecutor::Result result{};auto began=Clock::now();
    // Duplicate valid input bits only; shader and projection arithmetic remain
    // provider operations. Padding never enters raster scheduling or budgets.
    auto padded=inputs;padded.resize(s.count,inputs.back());
    auto shaded=s.vertex.execute(padded,vc);
    result.execution_device=shaded.execution_device;result.timings.shader_ms=ms(began);
    result.input_bytes=shaded.input_bytes;result.output_bytes=shaded.output_bytes;++result.provider_requests;
    std::vector<ClipPosition> clips(s.count);
    for(size_t v=0;v<s.count;++v)std::memcpy(clips[v].data(),shaded.vertices[v][s.vs.position_output].data(),16);
    began=Clock::now();auto projected=s.projection.execute(clips,viewport);require(projected.execution_device==result.execution_device);
    result.timings.projection_ms=ms(began);result.input_bytes+=uint64_t(s.count)*16+24;result.output_bytes+=uint64_t(s.count)*24+uint64_t(s.count/3)*16;++result.provider_requests;
    require(projected.triangle_bounds.size()==s.count/3);
    projected.vertices.resize(inputs.size());
    projected.triangle_bounds.resize(inputs.size()/3);
    shaded.vertices.resize(inputs.size());
    // Vertex count and shader graphs are bounded before compilation. Once the
    // device establishes geometry bounds, admit at most 4096 raster tiles.
    // Reject the whole draw before coverage or destination modification.
    // Keep the scheduler's existing tile-budget contract. Region addresses do not
    // depend on this conservative budget size; transport uses the separately
    // validated compact-provider batch capacity.
    const auto scheduling_batch=(std::min)(s.batch,16384u);
    const auto regions=bounded_triangle_regions(projected.triangle_bounds,clip,scheduling_batch,s.quads,false,scheduling_batch>8192);
    const auto layers=triangle_region_layers(regions);
    result.pixels=background;std::vector<Point> points(s.batch);
    std::vector<PackedColor> tile(s.batch);
    std::vector<size_t> triangle_base(s.batch),pixel_index(s.batch);
    std::vector<uint8_t> stored_pixel(s.batch);
    const auto submit=[&](size_t valid){
        require(valid && valid<=s.batch && (!s.quads || !(valid%4)));
        // Only filled addresses belong to this submission. The provider ABI
        // accepts partial spans, so do not shade synthetic padding samples.
        points.resize(valid);triangle_base.resize(valid);tile.resize(valid);
        for(size_t n=0;n<valid;++n)tile[n]=stored_pixel[n]?result.pixels[pixel_index[n]]:PackedColor{};
        began=Clock::now();const auto merged=s.program.execute(projected.vertices,shaded.vertices,points,triangle_base,fixed_clip,resources,tile,pc);require(merged.execution_device==result.execution_device);
        result.timings.merger_ms+=ms(began);result.input_bytes+=merged.input_bytes;result.output_bytes+=merged.output_bytes;result.provider_requests+=merged.provider_requests;result.covered_samples+=merged.covered_samples;
        for(size_t n=0;n<valid;++n)if(stored_pixel[n])result.pixels[pixel_index[n]]=merged.pixels[n];
        points.resize(s.batch);triangle_base.resize(s.batch);tile.resize(s.batch);
        ++result.tile_executions;
    };
    for(const auto &layer:layers){
        size_t filled=0;
        for(const auto triangle:layer){
            const auto &region=regions[triangle];const auto base=triangle*3;
            const size_t width=size_t(region[2]-region[0]),pixels=width*size_t(region[3]-region[1]);
            for(size_t first=0;first<pixels;){
                const auto count=(std::min)(size_t(s.batch)-filled,pixels-first);
                for(size_t k=0;k<count;++k){const auto i=first+k,n=filled+k;
                    // Bounds and scissor define addresses only; coverage and
                    // every color operation still run on the selected device.
                    const size_t x=s.quads?size_t(region[0])+2*((i/4)%(width/2))+(i&1):size_t(region[0])+i%width;
                    const size_t y=s.quads?size_t(region[1])+2*((i/4)/(width/2))+((i>>1)&1):size_t(region[1])+i/width;
                    points[n]={int32_t(x)*256+128,int32_t(y)*256+128};triangle_base[n]=base;
                    stored_pixel[n]=uint8_t(x<target_width && y<target_height);pixel_index[n]=y*target_width+x;
                }
                filled+=count;first+=count;
                if(filled==s.batch){submit(filled);filled=0;}
            }
        }
        if(filled)submit(filled);
    }
    result.vertices=std::move(shaded.vertices);result.timings.total_ms=ms(start);return result;
}
}
