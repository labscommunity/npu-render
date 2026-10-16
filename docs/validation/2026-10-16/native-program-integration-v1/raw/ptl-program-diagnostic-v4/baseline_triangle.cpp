#include "triangle_texture_draw.hpp"
#include "triangle_regions.hpp"
#include "color_interpolator.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <numeric>
#include <filesystem>
#include <fstream>
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
    bool quads,component_coverage,texture_reuse=false;
    PackedResources layout;
    shader::VertexExecutor vertex;
    ProjectionExecutor projection;
    std::unique_ptr<shader::PixelExecutor> pixel;
    CoverageExecutor coverage;
    OutputMerger merger;
    std::array<std::vector<unsigned>,2> varying_registers;
    std::array<std::unique_ptr<ColorInterpolator>,2> interpolators;
    State(const shader::Program &v,const shader::Program &p,uint32_t n,uint32_t w,uint32_t h,uint32_t f,
        const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,
        BlendMode blend,uint8_t mask,CullMode cull,bool ccw,uint32_t b)
        :vs(v),ps(p),count(n),width(w),height(h),format(f),batch(b<=shader::RegisterExecutor::maximum_batch(p)?b:std::gcd(b,shader::RegisterExecutor::maximum_batch(p))),quads(shader::quad_derivatives(p)),component_coverage(blend==BlendMode::component_coverage),layout(resource_layout(resources)),
         vertex(v,n,backend),projection(n,backend),coverage(batch,backend,cull,ccw),merger(batch,backend,blend,f==87,mask){
        char *setting=nullptr;size_t length=0;
        if(_dupenv_s(&setting,&length,"NR_RENDER_TEXTURE_SCOPE"))throw std::runtime_error("texture scope setting unavailable");
        if(setting){const std::string value(setting);std::free(setting);require(value=="0" || value=="1");texture_reuse=value=="1";}
        validate_shader(ps);
        require(n && n<=4096 && !(n%3) && w && w<=8192 && h && h<=8192 && uint64_t(w)*h<=8388576 &&
            b && b<=shader::RegisterExecutor::preferred_raster_batch(p) && (!quads || !(b%4)) &&
            (f==28 || f==87) && vs.stage==shader::Stage::vertex && vs.position_output<16);
        const auto shapes=resource_shapes(ps,resources);
        pixel=std::make_unique<shader::PixelExecutor>(ps,batch,backend,shapes,samplers);
        for(unsigned r=0;r<16;++r)if(ps.inputs[r] && r!=ps.position_input){
            require((vs.outputs[r]&ps.inputs[r])==ps.inputs[r]);
            if(ps.interpolation[r]==2)varying_registers[0].push_back(r);
            if(ps.interpolation[r]==4)varying_registers[1].push_back(r);
        }
        for(unsigned group=0;group<2;++group)if(!varying_registers[group].empty())
            interpolators[group]=std::make_unique<ColorInterpolator>(batch*uint32_t(varying_registers[group].size()),backend,group==0,true);
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
    const std::vector<PackedColor> &background,const std::optional<std::array<int32_t,4>> &scissor,const Color &blend_constant){
    PackedResources resources{};resources[0]=source;return execute(inputs,vc,pc,viewport,resources,background,scissor,blend_constant);
}
UniformDrawExecutor::Result TriangleTextureDrawExecutor::execute(const std::vector<shader::VertexRegisters> &inputs,
    const shader::VertexConstants &vc,const shader::VertexConstants &pc,const Viewport &viewport,const PackedResources &resources,
    const std::vector<PackedColor> &background,const std::optional<std::array<int32_t,4>> &scissor,const Color &blend_constant){
    auto &s=*state;const auto start=Clock::now();
    // Projection accepts viewport dimensions as provider inputs; coverage and
    // merger models operate on bounded batches, independent of target shape.
    require(viewport.x==0 && viewport.y==0 && std::isfinite(viewport.width) && std::isfinite(viewport.height) &&
        viewport.width>=1 && viewport.width<=8192 && viewport.height>=1 && viewport.height<=8192);
    const auto target_width=uint32_t(viewport.width),target_height=uint32_t(viewport.height);
    require(viewport.width==float(target_width) && viewport.height==float(target_height) &&
        uint64_t(target_width)*target_height<=8388576 && !inputs.empty() && inputs.size()<=s.count && !(inputs.size()%3) &&
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
    const auto regions=bounded_triangle_regions(projected.triangle_bounds,clip,s.batch,s.quads,false,s.batch>8192);
    const auto layers=triangle_region_layers(regions);
    const auto textures=decode_resources(s.ps,resources);
    auto texture_batch=s.pixel->bind_textures(textures,s.texture_reuse);uint64_t texture_upload_bytes=0,texture_reused_bytes=0;
    result.pixels=background;std::vector<Sample> samples(s.batch);std::vector<Point> points(s.batch);
    std::vector<PackedColor> tile(s.batch);std::vector<Color> colors(s.batch);std::vector<shader::VertexRegisters> fragments(s.batch);
    std::vector<std::array<ProjectedVertex,3>> vertices(s.batch);
    std::array<std::vector<std::array<ProjectedVertex,3>>,2> attribute_vertices;
    std::array<std::vector<std::array<Color,3>>,2> attribute_values;
    std::array<std::vector<Point>,2> attribute_points;
    std::array<std::vector<uint8_t>,2> attribute_coverage;
    for(unsigned group=0;group<2;++group){
        const auto n=size_t(s.batch)*s.varying_registers[group].size();
        attribute_vertices[group].resize(n);attribute_values[group].resize(n);
        attribute_points[group].resize(n);attribute_coverage[group].resize(n);
    }
    std::vector<size_t> triangle_base(s.batch),pixel_index(s.batch);
    std::vector<uint8_t> stored_pixel(s.batch);
    const auto submit=[&](size_t valid){
        require(valid && valid<=s.batch && (!s.quads || !(valid%4)));
        for(size_t n=0;n<s.batch;++n){
            if(n>=valid){points[n]={-128,-128};vertices[n]=vertices[0];triangle_base[n]=triangle_base[0];stored_pixel[n]=0;}
            samples[n]={{vertices[n][0].snapped,vertices[n][1].snapped,vertices[n][2].snapped},points[n]};
        }
        began=Clock::now();const auto covered=s.coverage.execute(samples,fixed_clip);require(covered.execution_device==result.execution_device);
        result.timings.coverage_ms+=ms(began);result.input_bytes+=covered.input_bytes;result.output_bytes+=uint64_t(s.batch)*4;++result.provider_requests;
        for(const auto v:covered.covered)result.covered_samples+=v;
        // The NPU has established that no samples survive. Skipping later
        // stages is control flow only; no host coverage/shader calculation.
        if(std::none_of(covered.covered.begin(),covered.covered.end(),[](auto v){return v!=0;})){++result.tile_executions;return;}
        auto interpolation_coverage=covered.covered;
        if(s.quads)for(size_t q=0;q<s.batch;q+=4){
            const bool active=covered.covered[q] || covered.covered[q+1] || covered.covered[q+2] || covered.covered[q+3];
            if(active)for(size_t lane=0;lane<4;++lane)interpolation_coverage[q+lane]=1;
        }
        for(unsigned r=0;r<16;++r)if(s.ps.inputs[r] && r!=s.ps.position_input && s.ps.interpolation[r]==1)
            for(size_t n=0;n<s.batch;++n)fragments[n][r]=shaded.vertices[triangle_base[n]][r];
        // Pack attributes with the same interpolation mode into one provider
        // request. The host only duplicates unchanged geometry and bits;
        // barycentric weights and all interpolation remain device arithmetic.
        for(unsigned group=0;group<2;++group){
            const auto &registers=s.varying_registers[group];if(registers.empty())continue;
            const auto count=registers.size();
            for(size_t n=0;n<s.batch;++n)for(size_t attribute=0;attribute<count;++attribute){
                const auto at=n*count+attribute;const auto r=registers[attribute];
                attribute_vertices[group][at]=vertices[n];attribute_points[group][at]=points[n];
                attribute_coverage[group][at]=interpolation_coverage[n];
                for(unsigned v=0;v<3;++v){
                    auto &value=attribute_values[group][at][v];value={};
                    for(unsigned c=0;c<4;++c)if(s.ps.inputs[r]&(1u<<c))
                        std::memcpy(&value[c],&shaded.vertices[triangle_base[n]+v][r][c],4);
                }
            }
            began=Clock::now();const auto interpolated=s.interpolators[group]->execute(attribute_vertices[group],attribute_values[group],attribute_points[group],attribute_coverage[group]);
            require(interpolated.execution_device==result.execution_device);result.timings.shader_ms+=ms(began);
            result.input_bytes+=uint64_t(s.batch)*count*96;result.output_bytes+=uint64_t(s.batch)*count*16;++result.provider_requests;
            for(size_t n=0;n<s.batch;++n)for(size_t attribute=0;attribute<count;++attribute)
                std::memcpy(fragments[n][registers[attribute]].data(),interpolated.colors[n*count+attribute].data(),16);
        }
        // The device supplied the execution mask. Inactive fragments cannot
        // contribute pixels or derivatives; use a live fragment/quad for their
        // input slots so padding cannot introduce invalid shader lookups.
        const auto first_live=size_t(std::find_if(covered.covered.begin(),covered.covered.end(),[](auto v){return v!=0;})-covered.covered.begin());
        if(s.quads){const auto live_quad=first_live&~size_t(3);
            for(size_t q=0;q<s.batch;q+=4)if(!interpolation_coverage[q])
                for(size_t lane=0;lane<4;++lane)fragments[q+lane]=fragments[live_quad+lane];
        }else for(size_t n=0;n<s.batch;++n)if(!covered.covered[n])fragments[n]=fragments[first_live];
        began=Clock::now();const auto pixels=texture_batch.execute(fragments,pc);require(pixels.execution_device==result.execution_device);
        texture_upload_bytes+=pixels.texture_upload_bytes;texture_reused_bytes+=pixels.texture_reused_bytes;result.timings.shader_ms+=ms(began);result.input_bytes+=pixels.input_bytes;result.output_bytes+=pixels.output_bytes;++result.provider_requests;
        for(size_t n=0;n<s.batch;++n)std::memcpy(colors[n].data(),pixels.vertices[n][0].data(),16);

        char *diagnostic=nullptr;size_t diagnostic_size=0;require(!_dupenv_s(&diagnostic,&diagnostic_size,"NR_RENDER_PTL_PROGRAM_DIAGNOSTIC"));const bool enabled=diagnostic&&*diagnostic=='1';std::free(diagnostic);

        if(enabled){
         char *directory=nullptr;size_t directory_size=0;require(!_dupenv_s(&directory,&directory_size,"NR_RENDER_PTL_PROGRAM_DIAGNOSTIC_DIR"));require(directory&&*directory);const std::filesystem::path path(directory);std::free(directory);
         std::ofstream stream(path/"synthetic-baseline-inputs.bin",std::ios::binary|std::ios::app);require(bool(stream));
         for(size_t n=0;n<valid;++n)if(stored_pixel[n]&&covered.covered[n]){stream.write(reinterpret_cast<const char*>(points[n].data()),8);stream.write(reinterpret_cast<const char*>(fragments[n][2].data()),16);stream.write(reinterpret_cast<const char*>(colors[n].data()),16);}require(bool(stream));
        }
        if(enabled)for(size_t n=0;n<valid;++n)if(stored_pixel[n]&&covered.covered[n]&&pixel_index[n]==108){
            const auto bits=[](float v){unsigned out;std::memcpy(&out,&v,4);return out;};
            std::printf("{\"kind\":\"baseline_rounding_input\",\"pixel\":108,\"input2_bits\":[");
            for(unsigned c=0;c<4;++c)std::printf("%s%u",c?",":"",fragments[n][2][c]);
            std::printf("],\"color_bits\":[");for(unsigned c=0;c<4;++c)std::printf("%s%u",c?",":"",bits(colors[n][c]));
            std::printf("],\"triangle\":%zu,\"texture_width\":%u,\"texture_height\":%u}\n",triangle_base[n],resources[0].width,resources[0].height);std::fflush(stdout);
        }
        for(size_t n=0;n<s.batch;++n)tile[n]=stored_pixel[n]?result.pixels[pixel_index[n]]:PackedColor{};
        began=Clock::now();const auto merged=s.merger.execute(colors,tile,covered.covered,blend_constant);require(merged.execution_device==result.execution_device);
        result.timings.merger_ms+=ms(began);result.input_bytes+=uint64_t(s.batch)*24+(s.component_coverage?16u:0u);result.output_bytes+=uint64_t(s.batch)*4;++result.provider_requests;
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
                    stored_pixel[n]=uint8_t(x<target_width && y<target_height);pixel_index[n]=y*target_width+x;
                }
                filled+=count;first+=count;
                if(filled==s.batch){submit(filled);filled=0;}
            }
        }
        if(filled)submit(filled);
    }
    std::printf("{\"texture_upload_scope\":true,\"host_uploaded_bytes\":%llu,\"host_recopy_bytes_avoided\":%llu,\"device_dma_measured\":false}\n",static_cast<unsigned long long>(texture_upload_bytes),static_cast<unsigned long long>(texture_reused_bytes));
    result.vertices=std::move(shaded.vertices);result.timings.total_ms=ms(start);return result;
}
}
