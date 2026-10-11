#include "triangle_texture_draw.hpp"
#include "triangle_regions.hpp"
#include "color_interpolator.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <numeric>
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
    bool quads;
    PackedResources layout;
    shader::VertexExecutor vertex;
    ProjectionExecutor projection;
    std::unique_ptr<shader::PixelExecutor> pixel;
    CoverageExecutor coverage;
    OutputMerger merger;
    std::unique_ptr<ColorInterpolator> perspective,affine;
    State(const shader::Program &v,const shader::Program &p,uint32_t n,uint32_t w,uint32_t h,uint32_t f,
        const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,
        BlendMode blend,uint8_t mask,CullMode cull,bool ccw,uint32_t b)
        :vs(v),ps(p),count(n),width(w),height(h),format(f),batch(b<=shader::RegisterExecutor::maximum_batch(p)?b:std::gcd(b,shader::RegisterExecutor::maximum_batch(p))),quads(shader::quad_derivatives(p)),layout(resource_layout(resources)),
         vertex(v,n,backend),projection(n,backend),coverage(batch,backend,cull,ccw),merger(batch,backend,blend,f==87,mask){
        validate_shader(ps);
        require(n && n<=4096 && !(n%3) && w && w<=8192 && h && h<=8192 && uint64_t(w)*h<=4*1024*1024 &&
            b && b<=4096 && (quads?!(b%4):!(uint64_t(w)*h%b)) &&
            (f==28 || f==87) && vs.stage==shader::Stage::vertex && vs.position_output<16);
        const auto shapes=resource_shapes(ps,resources);
        pixel=std::make_unique<shader::PixelExecutor>(ps,batch,backend,shapes,samplers);
        for(unsigned r=0;r<16;++r)if(ps.inputs[r] && r!=ps.position_input){
            require((vs.outputs[r]&ps.inputs[r])==ps.inputs[r]);
            if(ps.interpolation[r]==2 && !perspective)perspective=std::make_unique<ColorInterpolator>(batch,backend,true,true);
            if(ps.interpolation[r]==4 && !affine)affine=std::make_unique<ColorInterpolator>(batch,backend,false,true);
        }
    }
};
TriangleTextureDrawExecutor::TriangleTextureDrawExecutor(const shader::Program &v,const shader::Program &p,uint32_t n,uint32_t w,uint32_t h,uint32_t f,
    uint32_t tw,uint32_t th,uint32_t tf,const std::string &backend,
    bool blend,uint8_t mask,shader::TextureFilter filter,CullMode cull,bool ccw,uint32_t batch)
    :TriangleTextureDrawExecutor(v,p,n,w,h,f,tw,th,tf,backend,source_blend(blend),mask,filter,cull,ccw,batch){}
TriangleTextureDrawExecutor::TriangleTextureDrawExecutor(const shader::Program &v,const shader::Program &p,uint32_t n,uint32_t w,uint32_t h,uint32_t f,
    const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,
    bool blend,uint8_t mask,CullMode cull,bool ccw,uint32_t batch)
    :TriangleTextureDrawExecutor(v,p,n,w,h,f,resources,samplers,backend,source_blend(blend),mask,cull,ccw,batch){}
TriangleTextureDrawExecutor::TriangleTextureDrawExecutor(const shader::Program &v,const shader::Program &p,
    uint32_t n,uint32_t w,uint32_t h,uint32_t f,uint32_t sw,uint32_t sh,uint32_t sf,const std::string &backend,
    BlendMode blend,uint8_t mask,shader::TextureFilter filter,CullMode cull,bool ccw,uint32_t batch)
    :state(nullptr){
    PackedResources resources{};resources[0]={sw,sh,sf,{}};shader::TextureSamplers samplers{};samplers[0].filter=filter;
    state=std::make_unique<State>(v,p,n,w,h,f,resources,samplers,backend,blend,mask,cull,ccw,batch);
}
TriangleTextureDrawExecutor::TriangleTextureDrawExecutor(const shader::Program &v,const shader::Program &p,
    uint32_t n,uint32_t w,uint32_t h,uint32_t f,const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,
    BlendMode blend,uint8_t mask,CullMode cull,bool ccw,uint32_t batch)
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
    result.timings.projection_ms=ms(began);result.input_bytes+=uint64_t(s.count)*16+24;result.output_bytes+=uint64_t(s.count)*24+uint64_t(s.count/3)*16;++result.provider_requests;
    require(projected.triangle_bounds.size()==s.count/3);
    // Vertex count and shader graphs are bounded before compilation. Once the
    // device establishes geometry bounds, admit at most 4096 raster tiles.
    // Reject the whole draw before coverage or destination modification.
    const auto regions=bounded_triangle_regions(projected.triangle_bounds,clip,s.batch,s.quads);
    const auto layers=triangle_region_layers(regions);
    const auto textures=decode_resources(s.ps,resources);
    result.pixels=background;std::vector<Sample> samples(s.batch);std::vector<Point> points(s.batch);
    std::vector<PackedColor> tile(s.batch);std::vector<Color> colors(s.batch);std::vector<shader::VertexRegisters> fragments(s.batch);
    std::vector<std::array<ProjectedVertex,3>> vertices(s.batch);
    std::vector<std::array<Color,3>> values(s.batch);
    std::vector<size_t> triangle_base(s.batch),pixel_index(s.batch);
    std::vector<uint8_t> stored_pixel(s.batch);
    const auto submit=[&](size_t valid){
        require(valid && valid<=s.batch && (!s.quads || !(valid%4)));
        for(size_t n=0;n<s.batch;++n){
            if(n>=valid){points[n]={-128,-128};vertices[n]=vertices[0];triangle_base[n]=triangle_base[0];stored_pixel[n]=0;}
            samples[n]={{vertices[n][0].snapped,vertices[n][1].snapped,vertices[n][2].snapped},points[n]};
        }
        began=Clock::now();const auto covered=s.coverage.execute(samples,fixed_clip);require(covered.execution_device==result.execution_device);
        result.timings.coverage_ms+=ms(began);result.input_bytes+=uint64_t(s.batch)*32+16;result.output_bytes+=uint64_t(s.batch)*4;++result.provider_requests;
        for(const auto v:covered.covered)result.covered_samples+=v;
        // The NPU has established that no samples survive. Skipping later
        // stages is control flow only; no host coverage/shader calculation.
        if(std::none_of(covered.covered.begin(),covered.covered.end(),[](auto v){return v!=0;})){++result.tile_executions;return;}
        auto interpolation_coverage=covered.covered;
        if(s.quads)for(size_t q=0;q<s.batch;q+=4){
            const bool active=covered.covered[q] || covered.covered[q+1] || covered.covered[q+2] || covered.covered[q+3];
            if(active)for(size_t lane=0;lane<4;++lane)interpolation_coverage[q+lane]=1;
        }
        for(unsigned r=0;r<16;++r)if(s.ps.inputs[r] && r!=s.ps.position_input){
            if(s.ps.interpolation[r]==1){for(size_t n=0;n<s.batch;++n)fragments[n][r]=shaded.vertices[triangle_base[n]][r];continue;}
            for(size_t n=0;n<s.batch;++n)for(unsigned v=0;v<3;++v){
                values[n][v]={};
                for(unsigned c=0;c<4;++c)if(s.ps.inputs[r]&(1u<<c))
                    std::memcpy(&values[n][v][c],&shaded.vertices[triangle_base[n]+v][r][c],4);
            }
            began=Clock::now();const auto interpolated=(s.ps.interpolation[r]==2?s.perspective:s.affine)->execute(vertices,values,points,interpolation_coverage);
            require(interpolated.execution_device==result.execution_device);result.timings.shader_ms+=ms(began);
            result.input_bytes+=uint64_t(s.batch)*96;result.output_bytes+=uint64_t(s.batch)*16;++result.provider_requests;
            for(size_t n=0;n<s.batch;++n)std::memcpy(fragments[n][r].data(),interpolated.colors[n].data(),16);
        }
        // The device supplied the execution mask. Inactive fragments cannot
        // contribute pixels or derivatives; use a live fragment/quad for their
        // input slots so padding cannot introduce invalid shader lookups.
        const auto first_live=size_t(std::find_if(covered.covered.begin(),covered.covered.end(),[](auto v){return v!=0;})-covered.covered.begin());
        if(s.quads){const auto live_quad=first_live&~size_t(3);
            for(size_t q=0;q<s.batch;q+=4)if(!interpolation_coverage[q])
                for(size_t lane=0;lane<4;++lane)fragments[q+lane]=fragments[live_quad+lane];
        }else for(size_t n=0;n<s.batch;++n)if(!covered.covered[n])fragments[n]=fragments[first_live];
        began=Clock::now();const auto pixels=s.pixel->execute(fragments,pc,textures);require(pixels.execution_device==result.execution_device);
        result.timings.shader_ms+=ms(began);result.input_bytes+=pixels.input_bytes;result.output_bytes+=pixels.output_bytes;++result.provider_requests;
        for(size_t n=0;n<s.batch;++n)std::memcpy(colors[n].data(),pixels.vertices[n][0].data(),16);
        for(size_t n=0;n<s.batch;++n)tile[n]=stored_pixel[n]?result.pixels[pixel_index[n]]:PackedColor{};
        began=Clock::now();const auto merged=s.merger.execute(colors,tile,covered.covered);require(merged.execution_device==result.execution_device);
        result.timings.merger_ms+=ms(began);result.input_bytes+=uint64_t(s.batch)*24;result.output_bytes+=uint64_t(s.batch)*4;++result.provider_requests;
        for(size_t n=0;n<valid;++n)if(stored_pixel[n])result.pixels[pixel_index[n]]=merged.pixels[n];
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
                    vertices[n]={projected.vertices[base],projected.vertices[base+1],projected.vertices[base+2]};
                    stored_pixel[n]=uint8_t(x<s.width && y<s.height);pixel_index[n]=y*s.width+x;
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
