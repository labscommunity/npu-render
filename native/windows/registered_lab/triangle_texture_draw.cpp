#include "triangle_texture_draw.hpp"
#include "color_interpolator.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
namespace nr::raster {
namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void require(bool v){if(!v)throw std::invalid_argument("unsupported textured triangle draw");}
}
void TriangleTextureDrawExecutor::validate_shader(const shader::Program &p){
    require(p.stage==shader::Stage::pixel && p.outputs[0]==15);
    for(unsigned r=0;r<16;++r){
        if(r)require(!p.outputs[r]);
        if(p.inputs[r] && r!=p.position_input)require(p.interpolation[r]==1 || p.interpolation[r]==2 || p.interpolation[r]==4);
    }
    for(const auto &i:p.instructions)for(unsigned a=0;a<i.source_count;++a)
        require(i.sources[a].kind!=shader::Register::input || i.sources[a].index!=p.position_input);
    shader::RegisterExecutor::validate(p);
}
struct TriangleTextureDrawExecutor::State {
    shader::Program vs,ps;
    uint32_t count,width,height,format,batch;
    PackedResources layout;
    shader::VertexExecutor vertex;
    ProjectionExecutor projection;
    std::unique_ptr<shader::PixelExecutor> pixel;
    CoverageExecutor coverage;
    OutputMerger merger;
    std::unique_ptr<ColorInterpolator> perspective,affine;
    State(const shader::Program &v,const shader::Program &p,uint32_t n,uint32_t w,uint32_t h,uint32_t f,
        const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,
        bool blend,uint8_t mask,CullMode cull,bool ccw,uint32_t b)
        :vs(v),ps(p),count(n),width(w),height(h),format(f),batch(b),layout(resource_layout(resources)),
         vertex(v,n,backend),projection(n,backend),coverage(b,backend,cull,ccw),merger(b,backend,blend,f==87,mask){
        validate_shader(ps);
        require(n && n<=4096 && !(n%3) && w && w<=8192 && h && h<=8192 && uint64_t(w)*h<=4*1024*1024 &&
            b && b<=4096 && !(uint64_t(w)*h%b) && uint64_t(n/3)*(uint64_t(w)*h/b)<=4096 &&
            (f==28 || f==87) && vs.stage==shader::Stage::vertex && vs.position_output<16);
        const auto shapes=resource_shapes(ps,resources);
        pixel=std::make_unique<shader::PixelExecutor>(ps,batch,backend,shapes,samplers);
        for(unsigned r=0;r<16;++r)if(ps.inputs[r] && r!=ps.position_input){
            require((vs.outputs[r]&ps.inputs[r])==ps.inputs[r]);
            if(ps.interpolation[r]==2 && !perspective)perspective=std::make_unique<ColorInterpolator>(batch,backend,true);
            if(ps.interpolation[r]==4 && !affine)affine=std::make_unique<ColorInterpolator>(batch,backend,false);
        }
    }
};
TriangleTextureDrawExecutor::TriangleTextureDrawExecutor(const shader::Program &v,const shader::Program &p,
    uint32_t n,uint32_t w,uint32_t h,uint32_t f,uint32_t sw,uint32_t sh,uint32_t sf,const std::string &backend,
    bool blend,uint8_t mask,shader::TextureFilter filter,CullMode cull,bool ccw,uint32_t batch)
    :state(nullptr){
    PackedResources resources{};resources[0]={sw,sh,sf,{}};shader::TextureSamplers samplers{};samplers[0].filter=filter;
    state=std::make_unique<State>(v,p,n,w,h,f,resources,samplers,backend,blend,mask,cull,ccw,batch);
}
TriangleTextureDrawExecutor::TriangleTextureDrawExecutor(const shader::Program &v,const shader::Program &p,
    uint32_t n,uint32_t w,uint32_t h,uint32_t f,const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,
    bool blend,uint8_t mask,CullMode cull,bool ccw,uint32_t batch)
    :state(std::make_unique<State>(v,p,n,w,h,f,resources,samplers,backend,blend,mask,cull,ccw,batch)){}
TriangleTextureDrawExecutor::~TriangleTextureDrawExecutor()=default;
UniformDrawExecutor::Result TriangleTextureDrawExecutor::execute(const std::vector<shader::VertexRegisters> &inputs,
    const shader::VertexConstants &vc,const shader::VertexConstants &pc,const Viewport &viewport,const PackedTexture &source,
    const std::vector<PackedColor> &background,const std::optional<std::array<int32_t,4>> &scissor){
    PackedResources resources{};resources[0]=source;return execute(inputs,vc,pc,viewport,resources,background,scissor);
}
UniformDrawExecutor::Result TriangleTextureDrawExecutor::execute(const std::vector<shader::VertexRegisters> &inputs,
    const shader::VertexConstants &vc,const shader::VertexConstants &pc,const Viewport &viewport,const PackedResources &resources,
    const std::vector<PackedColor> &background,const std::optional<std::array<int32_t,4>> &scissor){
    auto &s=*state;const auto start=Clock::now();
    require(inputs.size()==s.count && background.size()==size_t(s.width)*s.height && resource_layout_equal(resources,s.layout) && viewport.x==0 && viewport.y==0 &&
        viewport.width==float(s.width) && viewport.height==float(s.height));
    const auto clip=scissor.value_or(std::array<int32_t,4>{0,0,int32_t(s.width),int32_t(s.height)});
    require(clip[0]>=0 && clip[1]>=0 && clip[0]<=clip[2] && clip[1]<=clip[3] && clip[2]<=int32_t(s.width) && clip[3]<=int32_t(s.height));
    const std::array<int32_t,4> fixed_clip{clip[0]*256,clip[1]*256,clip[2]*256,clip[3]*256};
    UniformDrawExecutor::Result result{};auto began=Clock::now();auto shaded=s.vertex.execute(inputs,vc);
    result.execution_device=shaded.execution_device;result.timings.shader_ms=ms(began);
    result.input_bytes=shaded.input_bytes;result.output_bytes=shaded.output_bytes;++result.provider_requests;
    std::vector<ClipPosition> clips(s.count);
    for(size_t v=0;v<s.count;++v)std::memcpy(clips[v].data(),shaded.vertices[v][s.vs.position_output].data(),16);
    began=Clock::now();const auto projected=s.projection.execute(clips,viewport);require(projected.execution_device==result.execution_device);
    result.timings.projection_ms=ms(began);result.input_bytes+=uint64_t(s.count)*16+24;result.output_bytes+=uint64_t(s.count)*24;++result.provider_requests;
    const auto textures=decode_resources(s.ps,resources);
    result.pixels=background;std::vector<Sample> samples(s.batch);std::vector<Point> points(s.batch);
    std::vector<PackedColor> tile(s.batch);std::vector<Color> colors(s.batch);std::vector<shader::VertexRegisters> fragments(s.batch);
    const size_t first_tile=(size_t(clip[1])*s.width/s.batch)*s.batch;
    const size_t end_tile=(std::min)(background.size(),((size_t(clip[3])*s.width+s.batch-1)/s.batch)*s.batch);
    for(size_t triangle=0;triangle<s.count;triangle+=3)for(size_t first=first_tile;first<end_tile;first+=s.batch){
        const std::array<ProjectedVertex,3> vertices{projected.vertices[triangle],projected.vertices[triangle+1],projected.vertices[triangle+2]};
        for(size_t n=0;n<s.batch;++n){
            const auto index=first+n;points[n]={int32_t(index%s.width)*256+128,int32_t(index/s.width)*256+128};
            samples[n]={{vertices[0].snapped,vertices[1].snapped,vertices[2].snapped},points[n]};
        }
        began=Clock::now();const auto covered=s.coverage.execute(samples,fixed_clip);require(covered.execution_device==result.execution_device);
        result.timings.coverage_ms+=ms(began);result.input_bytes+=uint64_t(s.batch)*32+16;result.output_bytes+=uint64_t(s.batch)*4;++result.provider_requests;
        for(const auto v:covered.covered)result.covered_samples+=v;
        for(unsigned r=0;r<16;++r)if(s.ps.inputs[r] && r!=s.ps.position_input){
            if(s.ps.interpolation[r]==1){for(auto &f:fragments)f[r]=shaded.vertices[triangle][r];continue;}
            std::array<Color,3> values{};
            for(unsigned v=0;v<3;++v)for(unsigned c=0;c<4;++c)if(s.ps.inputs[r]&(1u<<c))
                std::memcpy(&values[v][c],&shaded.vertices[triangle+v][r][c],4);
            began=Clock::now();const auto interpolated=(s.ps.interpolation[r]==2?s.perspective:s.affine)->execute(vertices,values,points,covered.covered);
            require(interpolated.execution_device==result.execution_device);result.timings.shader_ms+=ms(began);
            result.input_bytes+=uint64_t(s.batch)*12+84;result.output_bytes+=uint64_t(s.batch)*16;++result.provider_requests;
            for(size_t n=0;n<s.batch;++n)std::memcpy(fragments[n][r].data(),interpolated.colors[n].data(),16);
        }
        began=Clock::now();const auto pixels=s.pixel->execute(fragments,pc,textures);require(pixels.execution_device==result.execution_device);
        result.timings.shader_ms+=ms(began);result.input_bytes+=pixels.input_bytes;result.output_bytes+=pixels.output_bytes;++result.provider_requests;
        for(size_t n=0;n<s.batch;++n)std::memcpy(colors[n].data(),pixels.vertices[n][0].data(),16);
        std::copy_n(result.pixels.data()+first,s.batch,tile.data());
        began=Clock::now();const auto merged=s.merger.execute(colors,tile,covered.covered);require(merged.execution_device==result.execution_device);
        result.timings.merger_ms+=ms(began);result.input_bytes+=uint64_t(s.batch)*24;result.output_bytes+=uint64_t(s.batch)*4;++result.provider_requests;
        std::copy(merged.pixels.begin(),merged.pixels.end(),result.pixels.begin()+first);++result.tile_executions;
    }
    result.vertices=std::move(shaded.vertices);result.timings.total_ms=ms(start);return result;
}
}
