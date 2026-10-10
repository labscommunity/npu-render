// Offline replay of captured uniform-color triangle draws. No display control.
#include "capture_reader.hpp"
#include "blend_reference.hpp"
#include "provider_compile_statistics.hpp"
#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include "uniform_draw.hpp"
#include "point_texture_draw.hpp"
#include "linear_texture_draw.hpp"
#include "triangle_texture_draw.hpp"
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <cstdio>
namespace {
const char *phase="arguments";
nr::shader::VertexConstants constants(const nr::capture::Snapshot &s, unsigned stage, const nr::shader::Program &p) {
    nr::shader::VertexConstants result;
    for (unsigned i = 0; i < result.size(); ++i) if (p.constants[i]) {
        result[i] = s.values<nr::shader::VertexBits>("constants-" + std::to_string(stage) + "-" + std::to_string(i));
        nr::capture::check(result[i].size() >= p.constants[i]); result[i].resize(p.constants[i]);
    }
    return result;
}
std::vector<nr::shader::VertexRegisters> fetch(const nr::capture::Snapshot &s, uint32_t count, uint32_t first, int32_t base,
                                                   const nr::shader::Program &vs, bool indexed, uint32_t topology) {
    const auto elements = s.has("layout") ? s.values<uint32_t>("layout") : std::vector<uint32_t>{};
    nr::capture::check(elements.size() % 6 == 0);
    std::vector<uint32_t> indices, ids;
    if (indexed) {
        const auto id = s.values<uint32_t>("indices-description");
        nr::capture::check(topology == 4 && id.size() == 4 && ((id[1] == 2 && id[3] == 57) || (id[1] == 4 && id[3] == 42)));
        const auto &ib = s.bytes("indices");
        nr::capture::check(id[0] == ib.size() && uint64_t(id[2]) + (uint64_t(first) + count) * id[1] <= ib.size());
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t index = 0; std::memcpy(&index, ib.data() + id[2] + size_t(first + i) * id[1], id[1]);
            const int64_t adjusted = int64_t(index) + base; nr::capture::check(adjusted >= 0 && adjusted <= UINT32_MAX);
            indices.push_back(uint32_t(adjusted)); ids.push_back(index);
        }
    } else {
        nr::capture::check(count >= 3 && uint64_t(first) + count - 1 <= UINT32_MAX);
        auto append = [&](uint32_t id) { indices.push_back(first + id); ids.push_back(id); };
        if (topology == 4) { nr::capture::check(!(count % 3)); for(uint32_t i=0;i<count;++i) append(i); }
        else {
            nr::capture::check(topology == 5 && uint64_t(count-2)*3 <= 65536);
            for(uint32_t i=0;i<count-2;++i) { append(i+(i&1));append(i+((i&1)?0:1));append(i+2); }
        }
    }
    const auto instance_args=s.has("instances")?s.values<uint32_t>("instances"):std::vector<uint32_t>{1,0};
    nr::capture::check(instance_args.size()==2 && instance_args[0] && uint64_t(indices.size())*instance_args[0]<=65536);
    const size_t per_instance=indices.size();const auto original_indices=indices,original_ids=ids;
    for(uint32_t n=1;n<instance_args[0];++n){indices.insert(indices.end(),original_indices.begin(),original_indices.end());ids.insert(ids.end(),original_ids.begin(),original_ids.end());}
    count=uint32_t(indices.size());
    std::vector<nr::shader::VertexRegisters> result(count);
    uint32_t registers = 0;
    for (size_t e = 0; e < elements.size(); e += 6) {
        const auto slot = elements[e], offset = elements[e + 1], format = elements[e + 2], reg = elements[e + 5];
        nr::capture::check(slot < 16 && reg < 16 && reg != vs.vertex_id_input && reg != vs.instance_id_input && elements[e+3]<=1 && (elements[e+3] || !elements[e+4]) && !(registers & (1u << reg)));
        registers |= 1u << reg;
        const auto name = "vertices-" + std::to_string(slot);
        const auto description = s.values<uint32_t>(name + "-description"); const auto &vb = s.bytes(name);
        nr::capture::check(description.size() == 4 && description[0] == vb.size() && description[1] <= 2048);
        // Captured desktop layout formats: float1/2/3/4 and RGBA8 UNORM.
        const auto lanes = format == 2 ? 4u : (format == 6 ? 3u : (format == 16 ? 2u : (format == 41 ? 1u : 0u)));
        uint32_t integer_lanes=0,component_bytes=0;bool signed_integer=false;
        switch(format) {
        case 62:case 64:integer_lanes=1;component_bytes=1;signed_integer=format==64;break;
        case 50:case 52:integer_lanes=2;component_bytes=1;signed_integer=format==52;break;
        case 30:case 32:integer_lanes=4;component_bytes=1;signed_integer=format==32;break;
        case 57:case 59:integer_lanes=1;component_bytes=2;signed_integer=format==59;break;
        case 38:case 40:integer_lanes=2;component_bytes=2;signed_integer=format==40;break;
        case 12:case 14:integer_lanes=4;component_bytes=2;signed_integer=format==14;break;
        case 42:case 43:integer_lanes=1;component_bytes=4;break;
        case 17:case 18:integer_lanes=2;component_bytes=4;break;
        case 7:case 8:integer_lanes=3;component_bytes=4;break;
        case 3:case 4:integer_lanes=4;component_bytes=4;break;
        }
        nr::capture::check(lanes || format == 28 || integer_lanes);
        const auto bytes = integer_lanes ? integer_lanes*component_bytes : format == 28 ? 4u : lanes * 4;
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t begin = uint64_t(description[2]) + offset + (elements[e+3]?uint64_t(instance_args[1])+(elements[e+4]?(i/per_instance)/elements[e+4]:0):indices[i]) * description[1];
            nr::capture::check(begin <= vb.size() && bytes <= vb.size() - begin);
            auto &v = result[i][reg]; v = {0, 0, 0, integer_lanes?1u:0x3f800000u};
            if (format == 28) for (unsigned c = 0; c < 4; ++c) v[c] = nr::shader::as_bits(float(vb[size_t(begin) + c]) / 255);
            else if(integer_lanes)for(unsigned c=0;c<integer_lanes;++c){
                uint32_t raw=0;std::memcpy(&raw,vb.data()+size_t(begin)+c*component_bytes,component_bytes);
                const auto sign=1u<<(component_bytes*8-1);if(signed_integer && (raw&sign))raw|=~((sign<<1)-1);v[c]=raw;
            }
            else std::memcpy(v.data(), vb.data() + size_t(begin), bytes);
        }
    }
    for(uint32_t reg=0;reg<16;++reg) nr::capture::check(!vs.inputs[reg] || reg==vs.vertex_id_input || reg==vs.instance_id_input || (registers & (1u<<reg)));
    if(vs.vertex_id_input<16) for(uint32_t n=0;n<count;++n) result[n][vs.vertex_id_input][0]=ids[n];
    if(vs.instance_id_input<16)for(uint32_t n=0;n<count;++n)result[n][vs.instance_id_input][0]=uint32_t(n/per_instance);
    return result;
}
}
int main(int argc, char **argv) try {
    using namespace nr::shader;
    using namespace nr::raster;
    if ((argc != 7 && argc != 11) || std::strcmp(argv[1], "--capture") || std::strcmp(argv[3], "--backend") || std::strcmp(argv[5], "--output"))
        throw std::invalid_argument("expected --capture <NPDRAW01 file> --backend <CPU|NPU> --output <new pixel file>");
    uint32_t repeats = 1, batch = 4096;
    if (argc == 11) {
        nr::capture::check(!std::strcmp(argv[7], "--repeat") && !std::strcmp(argv[9], "--batch"));
        char *end = nullptr; const auto r = std::strtoul(argv[8], &end, 10);
        nr::capture::check(end != argv[8] && !*end && r && r <= 5); repeats = uint32_t(r);
        const auto b = std::strtoul(argv[10], &end, 10);
        nr::capture::check(end != argv[10] && !*end && b && b <= 4096); batch = uint32_t(b);
    }
    const std::string backend = argv[4]; nr::capture::check(backend == "CPU" || backend == "NPU");
    std::ifstream existing(argv[6], std::ios::binary); nr::capture::check(!existing); existing.close();
    const auto start = std::chrono::steady_clock::now();
    phase="capture decode";const nr::capture::Snapshot snapshot(argv[2]);
    auto args = snapshot.values<uint32_t>("arguments");
    const auto call = snapshot.has("draw-indexed") ? snapshot.values<uint32_t>("draw-indexed") : std::vector<uint32_t>{1};
    nr::capture::check(call.size()==1 && call[0]<=1); const bool indexed=call[0]!=0;
    nr::capture::check(args.size() == 9 && args[0] && args[0] <= 65536 && (args[3] == 4 || (!indexed && args[3] == 5)) && args[4] == 1 && args[7] == UINT32_MAX);
    const auto view = snapshot.values<float>("viewport");
    const auto surface = snapshot.values<uint32_t>("target-description");
    nr::capture::check(view.size() == 6 && surface.size() == 10 && surface[0] == 0x4e52574b && surface[1] == 1 && surface[2] == 40 && !surface[7]);
    const uint32_t width = surface[3], height = surface[4], format = surface[5];
    nr::capture::check(width && width <= 8192 && height && height <= 8192 && (format == 28 || format == 87) &&
          view[0] == 0 && view[1] == 0 && view[2] == float(width) && view[3] == float(height));
    const auto blend = snapshot.values<uint32_t>("blend"), raster = snapshot.values<uint32_t>("raster"), depth = snapshot.values<uint32_t>("depth");
    nr::capture::check(blend.size() == 17 && !blend[0] && depth.size() == 15 && !depth[0] && !depth[3]);
    // Only target zero is bound in this captured draw; other blend slots are unused.
    BlendMode blend_mode=BlendMode::source_copy;
    if(blend[1]){
        nr::capture::check(blend[11]==1 && blend[14]==1 && blend[9]==blend[12] && blend[10]==blend[13]);
        constexpr std::array<uint32_t,6> ddi_factors{1,2,5,6,7,8};bool found=false;
        for(uint32_t mode=0;mode<blend_factors.size();++mode){const auto pair=blend_factors[mode];
            if(blend[9]==ddi_factors[uint32_t(pair[0])] && blend[10]==ddi_factors[uint32_t(pair[1])]){blend_mode=BlendMode(mode);found=true;break;}
        }
        nr::capture::check(found);
    }
    const bool blending=blend_mode!=BlendMode::source_copy;
    nr::capture::check(raster.size() == 10 && raster[0] == 3 && raster[1] >= 1 && raster[1] <= 3 && !raster[3] && !raster[4] && !raster[5] && !raster[8] && !raster[9]);
    std::array<int32_t,4> clip{0,0,int32_t(width),int32_t(height)};
    if (raster[7]) {
        const auto scissor = snapshot.values<int32_t>("scissors");
        nr::capture::check(scissor.size() == 4 && scissor[0] >= 0 && scissor[1] >= 0 && scissor[0] <= scissor[2] && scissor[1] <= scissor[3] && scissor[2] <= int32_t(width) && scissor[3] <= int32_t(height));
        std::copy(scissor.begin(),scissor.end(),clip.begin());
    }
    auto pixels = snapshot.values<PackedColor>("target"); nr::capture::check(pixels.size() == size_t(width) * height);
    const auto vs_words = snapshot.values<uint32_t>("vertex-shader"), ps_words = snapshot.values<uint32_t>("pixel-shader");
    phase="shader parse";const auto vs = parse_vertex(vs_words.data(), vs_words.size()), ps = parse_pixel(ps_words.data(), ps_words.size());
    nr::capture::check(vs.position_output < 16 && ps.outputs[0] == 15);
    for (size_t i = 1; i < ps.outputs.size(); ++i) nr::capture::check(!ps.outputs[i]);
    const bool textured = std::any_of(ps.textures.begin(), ps.textures.end(), [](bool v) { return v; });
    // Uniform specialization is valid only when PS does not read position.
    for (const auto &instruction : ps.instructions) for (unsigned i = 0; i < instruction.source_count; ++i)
        nr::capture::check(instruction.sources[i].kind != Register::input || instruction.sources[i].index != ps.position_input);
    int32_t base; std::memcpy(&base, &args[2], 4);
    phase="vertex fetch";const auto inputs = fetch(snapshot, args[0], args[1], base, vs, indexed, args[3]);
    args[0]=uint32_t(inputs.size()); nr::capture::check(!(args[0]%3));
    const auto cb = constants(snapshot, 0, vs);
    const auto pixel_cb = constants(snapshot, 2, ps);
    phase="executor construction";const auto compile_start = std::chrono::steady_clock::now();
    std::unique_ptr<UniformDrawExecutor> executor;
    std::unique_ptr<PointTextureDrawExecutor> texture_executor;
    std::unique_ptr<LinearTextureDrawExecutor> linear_executor;
    std::unique_ptr<TriangleTextureDrawExecutor> triangle_executor;
    TextureShapes texture_shapes{};TexturePixels texture_pixels{};TextureSamplers texture_samplers{};
    std::vector<std::array<double,4>> linear_reference;
    uint64_t linear_covered=0;
    PackedTexture texture;PackedResources resources{};
    bool general_resources=quad_derivatives(ps);
    for(unsigned slot=0;slot<16;++slot)general_resources=general_resources ||
        (ps.textures[slot] && (slot || ps.texture_dimensions[slot]!=3 || ps.texture_uint[slot])) || (slot && ps.samplers[slot]);
    for(const auto &instruction:ps.instructions)general_resources=general_resources || instruction.opcode==Opcode::load;
    for(unsigned slot=0;slot<16;++slot)if(ps.samplers[slot]){
        const auto sampler=snapshot.values<uint32_t>("sampler-2-"+std::to_string(slot));nr::capture::check(sampler.size()==13);
        general_resources=general_resources || sampler[1]!=3 || sampler[2]!=3;
    }
    std::vector<PackedColor> texture_reference;
    if(general_resources){
        for(unsigned slot=0;slot<16;++slot){
            if(ps.samplers[slot]){
                const auto sampler=snapshot.values<uint32_t>("sampler-2-"+std::to_string(slot));
                nr::capture::check(sampler.size()==13 && (sampler[0]==0 || sampler[0]==21) && (sampler[1]==2 || sampler[1]==3) && (sampler[2]==2 || sampler[2]==3) && sampler[3]==3 && !sampler[4] && !sampler[11] && as_float(sampler[12])>=0);
                texture_samplers[slot].filter=sampler[0]==0?TextureFilter::point:TextureFilter::linear;
                texture_samplers[slot].u=sampler[1]==2?TextureAddress::mirror:TextureAddress::clamp;
                texture_samplers[slot].v=sampler[2]==2?TextureAddress::mirror:TextureAddress::clamp;
            }
            if(!ps.textures[slot])continue;const auto name="resource-2-"+std::to_string(slot);if(!snapshot.has(name))continue;
            const auto description=snapshot.values<uint32_t>(name+"-description"),view_desc=snapshot.values<uint32_t>(name+"-view");
            nr::capture::check(description.size()==10 && view_desc.size()==4);auto &t=resources[slot];
            if(view_desc[3]){
                const auto &data=snapshot.bytes(name);nr::capture::check(view_desc[0]==62 && uint64_t(view_desc[1])+view_desc[2]<=data.size());
                t={view_desc[2],1,42,{}};t.pixels.resize(view_desc[2]);
                for(size_t n=0;n<t.pixels.size();++n){const uint32_t v=data[size_t(view_desc[1])+n];std::memcpy(t.pixels[n].data(),&v,4);}
            }else if(description[5]==65){const auto &data=snapshot.bytes(name);t={description[3],description[4],28,{}};
                nr::capture::check(data.size()==size_t(t.width)*t.height);t.pixels.resize(data.size());for(size_t n=0;n<data.size();++n)t.pixels[n]={0,0,0,data[n]};
            }else t={description[3],description[4],description[5],snapshot.values<PackedColor>(name)};
        }
        texture_shapes=resource_shapes(ps,resources);texture_pixels=decode_resources(ps,resources);
        if(quad_derivatives(ps)){while(batch>4 && uint64_t(clip[2]-clip[0])*uint64_t(clip[3]-clip[1])<=batch/2)batch/=2;batch=(std::max)(4u,batch);}
        else while(uint64_t(width)*height%batch)batch/=2;
        triangle_executor=std::make_unique<TriangleTextureDrawExecutor>(vs,ps,args[0],width,height,format,resources,texture_samplers,backend,
            blend_mode,uint8_t(blend[15]&255),static_cast<CullMode>(raster[1]-1),raster[2]!=0,batch);
    }else if (textured) {
        const auto source = snapshot.values<uint32_t>("source-description"), sampler = snapshot.values<uint32_t>("sampler-2-0");
        nr::capture::check(source.size() == 10 && source[0] == 0x4e52574b && source[1] == 1 && source[2] == 40 && !source[7]);
        nr::capture::check(sampler.size() == 13 && (sampler[0]==0 || sampler[0]==21) && sampler[1] == 3 && sampler[2] == 3 && sampler[3] == 3 && !sampler[4] && !sampler[11]);
        nr::capture::check(as_float(sampler[12]) >= 0);
        if(source[5] == 65) {
            const auto alpha=snapshot.values<uint8_t>("source");
            nr::capture::check(alpha.size()==uint64_t(source[3])*source[4]);
            texture={source[3],source[4],28,{}};texture.pixels.resize(alpha.size());
            for(size_t i=0;i<alpha.size();++i)texture.pixels[i]={0,0,0,alpha[i]};
        } else texture = {source[3],source[4],source[5],snapshot.values<PackedColor>("source")};
        bool general_texture=general_blend(blend_mode) || args[0]!=6 || raster[1]!=1 || (blend[15]&255)!=15;
        if(!general_texture)try{LinearTextureDrawExecutor::validate_shader(ps);}catch(const std::invalid_argument &){general_texture=true;}
        if(general_texture){
            if(quad_derivatives(ps)){while(batch>4 && uint64_t(clip[2]-clip[0])*uint64_t(clip[3]-clip[1])<=batch/2)batch/=2;batch=(std::max)(4u,batch);}
        else while(uint64_t(width)*height%batch)batch/=2;
            triangle_executor=std::make_unique<TriangleTextureDrawExecutor>(vs,ps,args[0],width,height,format,texture.width,texture.height,texture.format,
                backend,blend_mode,uint8_t(blend[15]&255),sampler[0]==0?TextureFilter::point:TextureFilter::linear,static_cast<CullMode>(raster[1]-1),raster[2]!=0,batch);
            texture_shapes[0]={texture.width,texture.height};texture_samplers[0].filter=sampler[0]==0?TextureFilter::point:TextureFilter::linear;
            texture_pixels[0].resize(texture.pixels.size());
            for(size_t n=0;n<texture.pixels.size();++n)for(unsigned c=0;c<4;++c)
                texture_pixels[0][n][c]=float(texture.pixels[n][texture.format==87 && c<3?2-c:c])/255;
        }else if(sampler[0]==21 || ps.instructions.size()==2 || blending || texture.width<width || texture.height<height || clip!=std::array<int32_t,4>{0,0,int32_t(width),int32_t(height)}){
            const bool point=sampler[0]==0;
            linear_executor=std::make_unique<LinearTextureDrawExecutor>(vs,ps,width,height,format,texture.width,texture.height,texture.format,backend,blending,point?TextureFilter::point:TextureFilter::linear);
            const auto a=reference_vertex(vs,inputs[0],cb),b=reference_vertex(vs,inputs[2],cb);const auto pr=vs.position_output,ur=ps.instructions[0].sources[0].index;
            const std::array<float,4> screen{(as_float(a[pr][0])+1)*(float(width)*.5f),(1-as_float(a[pr][1]))*(float(height)*.5f),
                (as_float(b[pr][0])+1)*(float(width)*.5f),(1-as_float(b[pr][1]))*(float(height)*.5f)};
            const std::array<double,4> uv{as_float(a[ur][0]),as_float(a[ur][1]),as_float(b[ur][0]),as_float(b[ur][1])};
            const auto cr=ps.instructions.back().sources[1].index;
            const std::array<double,4> color{as_float(a[cr][0]),as_float(a[cr][1]),as_float(a[cr][2]),as_float(a[cr][3])};
            std::array<int32_t,4> snapped{};for(unsigned c=0;c<4;++c)snapped[c]=int32_t(std::nearbyint(double(screen[c])*256));
            linear_reference.resize(size_t(width)*height);
            for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){
                const auto at=size_t(y)*width+x;const int32_t px=int32_t(x)*256+128,py=int32_t(y)*256+128;
                const bool covered=px>=snapped[0]&&px<snapped[2]&&py>=snapped[1]&&py<snapped[3]&&int32_t(x)>=clip[0]&&int32_t(y)>=clip[1]&&int32_t(x)<clip[2]&&int32_t(y)<clip[3];
                if(!covered){for(unsigned c=0;c<4;++c)linear_reference[at][c]=pixels[at][c];continue;}++linear_covered;
                const double tx=(uv[0]+(x+.5-double(snapped[0])/256)/(double(snapped[2]-snapped[0])/256)*(uv[2]-uv[0]))*texture.width-(point?0:.5);
                const double ty=(uv[1]+(y+.5-double(snapped[1])/256)/(double(snapped[3]-snapped[1])/256)*(uv[3]-uv[1]))*texture.height-(point?0:.5);
                const auto ix=int64_t(std::floor(tx)),iy=int64_t(std::floor(ty));const double fx=point?0:tx-double(ix),fy=point?0:ty-double(iy);double sample[4]{};
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto sx=std::clamp(ix+dx,int64_t(0),int64_t(texture.width)-1),sy=std::clamp(iy+dy,int64_t(0),int64_t(texture.height)-1);
                    for(unsigned c=0;c<4;++c)sample[c]+=texture.pixels[size_t(sy)*texture.width+size_t(sx)][c]*(dx?fx:1-fx)*(dy?fy:1-fy);}
                if(ps.instructions.size()==3)sample[3]=255;
                for(unsigned c=0;c<4;++c)linear_reference[at][c]=std::clamp(sample[format!=texture.format&&c<3?2-c:c]*color[format==87&&c<3?2-c:c]+(blending?pixels[at][c]*(1-sample[3]*color[3]/255):0),0.,255.);
            }
        }else{
            nr::capture::check(!blending && clip==std::array<int32_t,4>{0,0,int32_t(width),int32_t(height)});
        texture_executor = std::make_unique<PointTextureDrawExecutor>(vs,ps,width,height,format,texture.width,texture.height,texture.format,backend);
        // Independent scalar point-sampling oracle for this capture. The runtime
        // specialization validates the complete shader and affine-quad proof.
        const auto first_vertex = reference_vertex(vs,inputs[0],cb);
        const auto uv_register = ps.instructions[0].sources[0].index;
        const double ox = double(as_float(first_vertex[uv_register][0]))*texture.width;
        const double oy = double(as_float(first_vertex[uv_register][1]))*texture.height;
        texture_reference.resize(size_t(width)*height);
        for (uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
            const auto sx = int64_t(std::floor(ox+x+.5)), sy = int64_t(std::floor(oy+y+.5));
            nr::capture::check(sx>=0 && sy>=0 && sx<texture.width && sy<texture.height);
            auto p = texture.pixels[size_t(sy)*texture.width+size_t(sx)];
            if(format!=texture.format) std::swap(p[0],p[2]); p[3]=255; texture_reference[size_t(y)*width+x]=p;
        }
        }
    } else {
        if(quad_derivatives(ps)){while(batch>4 && uint64_t(clip[2]-clip[0])*uint64_t(clip[3]-clip[1])<=batch/2)batch/=2;batch=(std::max)(4u,batch);}
        else while(uint64_t(width)*height%batch)batch/=2;
        executor = std::make_unique<UniformDrawExecutor>(vs, ps, args[0], width, height, backend, blend_mode, format == 87, uint8_t(blend[15] & 255), batch,static_cast<CullMode>(raster[1]-1),raster[2]!=0);
    }
    const auto compile_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - compile_start).count();
    const auto cache=nr::provider::compile_statistics();
    std::printf("{\"kind\":\"model_compile_cache\",\"compile_calls\":%llu,\"cache_requests\":%llu,\"cache_hits\":%llu,\"cache_status_unavailable\":%llu}\n",
        static_cast<unsigned long long>(cache.calls),static_cast<unsigned long long>(cache.cache_requests),static_cast<unsigned long long>(cache.cache_hits),static_cast<unsigned long long>(cache.cache_status_unavailable));
    UniformDrawExecutor::Result result;
    std::vector<PackedColor> reference_pixels;
    for (uint32_t iteration = 0; iteration < repeats; ++iteration) {
        const Viewport viewport{view[0],view[1],view[2],view[3],view[4],view[5]};
        phase="provider execute";result = general_resources ? triangle_executor->execute(inputs,cb,pixel_cb,viewport,resources,pixels,clip) : triangle_executor ? triangle_executor->execute(inputs,cb,pixel_cb,viewport,texture,pixels,clip) : linear_executor ? linear_executor->execute(inputs,cb,viewport,texture,pixels,clip) : textured ? texture_executor->execute(inputs,cb,viewport,texture) : executor->execute(inputs, cb, pixel_cb, viewport, pixels, clip);
        uint64_t fused_matches=0;
        phase="vertex oracle";for (size_t i = 0; i < inputs.size(); ++i) {
            const auto expected = reference_vertex(vs, inputs[i], cb);
            const auto fused = reference_vertex(vs, inputs[i], cb, {}, {}, {}, 1);
            const auto reverse_fused = reference_vertex(vs, inputs[i], cb, {}, {}, {}, 2);
            for (unsigned r = 0; r < 16; ++r) for (unsigned c = 0; c < 4; ++c) if (vs.outputs[r] & (1u << c))
                if(result.vertices[i][r][c]!=expected[r][c]){
                    if(result.vertices[i][r][c]==fused[r][c] || result.vertices[i][r][c]==reverse_fused[r][c]){++fused_matches;continue;}
                    std::fprintf(stderr,"vertex mismatch v=%zu register=%u lane=%u actual=%08x expected=%08x\n",i,r,c,result.vertices[i][r][c],expected[r][c]);
                    nr::capture::check(false);
                }
        }
        std::printf("{\"vertex_oracle_fused_arithmetic_matches\":%llu}\n",static_cast<unsigned long long>(fused_matches));
        phase="pixel oracle";if(linear_executor){
            double maximum=0;uint64_t differing=0;
            nr::capture::check(result.covered_samples==linear_covered);
            for(size_t n=0;n<result.pixels.size();++n)for(unsigned c=0;c<4;++c){const auto wanted=linear_reference[n][c];
                maximum=(std::max)(maximum,std::abs(result.pixels[n][c]-wanted));if(result.pixels[n][c]!=uint8_t(std::nearbyint(wanted)))++differing;}
            nr::capture::check(maximum<=.6);
            std::printf("{\"linear_oracle_max_byte_error\":%.9g,\"rounded_oracle_differences\":%llu}\n",maximum,static_cast<unsigned long long>(differing));
        }else if (textured && !triangle_executor) nr::capture::check(result.pixels == texture_reference);
        else {
            VertexRegisters uniform{};
            for (unsigned r = 0; r < 16; ++r) if (ps.inputs[r] && r != ps.position_input) uniform[r] = result.vertices[0][r];
            bool is_uniform=!triangle_executor;
            for(const auto &v:result.vertices)for(unsigned r=0;r<16;++r)if(ps.inputs[r] && r!=ps.position_input)
                for(unsigned c=0;c<4;++c)if(ps.inputs[r]&(1u<<c))is_uniform=is_uniform && v[r][c]==uniform[r][c];
            if(is_uniform)nr::capture::check(result.fragment == reference_vertex(ps, uniform, pixel_cb)[0]);
            // Independent scalar coverage and byte-color oracle for this replay.
            // A transparent draw still must preserve every destination byte.
            auto expected=pixels;uint64_t covered=0;
            std::vector<Point> points(inputs.size());
            std::vector<std::array<double,3>> screen(inputs.size());
            for(size_t i=0;i<inputs.size();++i) {
                const auto v=result.vertices[i][vs.position_output]; // Verified against scalar fused/unfused shader evaluations above.
                volatile float nx=as_float(v[0])/as_float(v[3]),ny=as_float(v[1])/as_float(v[3]);
                volatile float sx=nx+1,sy=1-ny;sx=sx*(float(width)*.5f);sy=sy*(float(height)*.5f);
                points[i]={int32_t(std::nearbyint(float(sx)*256)),int32_t(std::nearbyint(float(sy)*256))};
                screen[i]={float(sx),float(sy),1./as_float(v[3])};
            }
            const auto edge=[](Point a,Point b,Point p){return (int64_t(b[0])-a[0])*(int64_t(p[1])-a[1])-(int64_t(b[1])-a[1])*(int64_t(p[0])-a[0]);};
            for(size_t t=0;t<points.size();t+=3) {
                std::array<Point,3> triangle{points[t],points[t+1],points[t+2]};const auto area=edge(triangle[0],triangle[1],triangle[2]);
                const bool front=(area<0)==(raster[2]!=0);
                if(!area || (raster[1]==2 && front) || (raster[1]==3 && !front))continue;
                if(area<0)std::swap(triangle[1],triangle[2]);
                for(int32_t y=clip[1];y<clip[3];++y)for(int32_t x=clip[0];x<clip[2];++x){
                    const Point p{x*256+128,y*256+128};bool inside=true;
                    for(size_t e=0;e<3;++e){const auto a=triangle[e],b=triangle[(e+1)%3];const auto value=edge(a,b,p);const bool top=b[1]<a[1]||(b[1]==a[1]&&b[0]>a[0]);if(value<0||(!value&&!top))inside=false;}
                    if(!inside)continue;++covered;
                    auto fragment=result.fragment;
                    if(!is_uniform) {
                        const auto interpolate_at=[&](double px,double py){
                            VertexRegisters interpolated{};
                            for(unsigned r=0;r<16;++r)if(ps.inputs[r] && r!=ps.position_input){
                                const auto mode=ps.interpolation[r];
                                if(mode==1){interpolated[r]=result.vertices[t][r];continue;}
                                nr::capture::check(mode==2 || mode==4);
                                double weight[3]{},denominator=0;
                                for(unsigned v=0;v<3;++v){const auto &a=screen[t+(v+1)%3],&b=screen[t+(v+2)%3];
                                    weight[v]=((b[0]-a[0])*(py-a[1])-(b[1]-a[1])*(px-a[0]))*(mode==2?screen[t+v][2]:1);denominator+=weight[v];}
                                nr::capture::check(denominator!=0);
                                for(unsigned c=0;c<4;++c)if(ps.inputs[r]&(1u<<c)){
                                    double value=0;for(unsigned v=0;v<3;++v)value+=weight[v]*as_float(result.vertices[t+v][r][c]);
                                    interpolated[r][c]=as_bits(float(value/denominator));
                                }
                            }
                            return interpolated;
                        };
                        if(quad_derivatives(ps)){
                            std::array<VertexRegisters,4> quad;
                            for(unsigned lane=0;lane<4;++lane)quad[lane]=interpolate_at((x&~1)+(lane&1)+.5,(y&~1)+(lane>>1)+.5);
                            fragment=reference_pixel_quad(ps,quad,pixel_cb,texture_shapes,texture_samplers,texture_pixels)[unsigned(x&1)|unsigned((y&1)<<1)][0];
                        }else fragment=reference_vertex(ps,interpolate_at(x+.5,y+.5),pixel_cb,texture_shapes,texture_samplers,texture_pixels)[0];
                    }
                    auto &pixel=expected[size_t(y)*width+size_t(x)];
                    for(unsigned c=0;c<4;++c){
                        const auto lane=format==87 && c<3 ? 2-c : c;
                        if(!((blend[15]&255)&(1u<<lane)))continue;
                        const double alpha=std::clamp(double(as_float(fragment[3])),0.,1.);
                        const auto terms=reference_blend_terms(blend_mode,std::clamp(double(as_float(fragment[lane])),0.,1.),alpha,double(pixel[c]),double(pixel[3]));
                        const double value=terms[0]+terms[1];
                        pixel[c]=uint8_t(std::nearbyint((std::clamp)(value,0.,255.)));
                    }
                }
            }
            unsigned maximum=0;uint64_t differences=0;
            for(size_t i=0;i<expected.size();++i)for(unsigned c=0;c<4;++c){const auto error=unsigned(std::abs(int(result.pixels[i][c])-int(expected[i][c])));maximum=(std::max)(maximum,error);differences+=error!=0;}
            if(result.covered_samples!=covered || maximum>(is_uniform?0u:1u))
                std::fprintf(stderr,"oracle mismatch covered=%llu expected=%llu maximum_byte_error=%u differing_components=%llu\n",static_cast<unsigned long long>(result.covered_samples),static_cast<unsigned long long>(covered),maximum,static_cast<unsigned long long>(differences));
            nr::capture::check(result.covered_samples==covered && maximum<=(is_uniform?0u:1u));
            std::printf("{\"uniform_inputs\":%s,\"oracle_max_byte_error\":%u,\"differing_components\":%llu,\"coverage_exact\":true}\n",is_uniform?"true":"false",maximum,static_cast<unsigned long long>(differences));
        }
        if (!iteration) reference_pixels = result.pixels; else nr::capture::check(result.pixels == reference_pixels);
        const auto &t = result.timings;
        std::printf("{\"kind\":\"draw_stage_timing\",\"iteration\":%u,\"batch\":%u,\"compile_ms\":%.3f,\"shader_ms\":%.3f,\"projection_ms\":%.3f,\"coverage_ms\":%.3f,\"merger_ms\":%.3f,\"draw_ms\":%.3f,\"rectangle_path\":%s,\"execution_device\":\"%s\"}\n",
            iteration, batch, compile_ms, t.shader_ms, t.projection_ms, t.coverage_ms, t.merger_ms, t.total_ms, result.used_rectangle ? "true" : "false", result.execution_device.c_str());
    }
    pixels = std::move(result.pixels);
    std::ofstream output(argv[6], std::ios::binary); nr::capture::check(bool(output));
    output.write(reinterpret_cast<const char *>(pixels.data()), std::streamsize(pixels.size() * sizeof(PackedColor))); output.close(); nr::capture::check(bool(output));
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("{\"kind\":\"offline_draw_replay\",\"textured\":%s,\"width\":%u,\"height\":%u,\"vertices\":%u,\"triangles\":%u,\"covered_samples\":%llu,\"tile_executions\":%llu,\"execution_device\":\"%s\",\"elapsed_ms\":%lld,\"displayed_frames\":0,\"desktop_offload_tested\":false}\n",
        textured ? "true" : "false", width, height, args[0], args[0] / 3, static_cast<unsigned long long>(result.covered_samples), static_cast<unsigned long long>(result.tile_executions),
        result.execution_device.c_str(), static_cast<long long>(milliseconds));
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s: %s\n", phase, e.what()); return 1; }
