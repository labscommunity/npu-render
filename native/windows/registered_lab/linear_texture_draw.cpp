#include "provider_compile.hpp"
#include "linear_texture_draw.hpp"
#include "triangle_regions.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <cmath>
#include <cstring>
#include <chrono>
#include <sstream>
namespace nr::raster {
namespace {
void require(bool v) { if (!v) throw std::invalid_argument("unsupported linear-textured rectangle draw"); }
bool plain(const shader::Operand &o, shader::Register kind, uint32_t index) {
    return o.kind == kind && o.index == index && !o.element && !o.relative.enabled && o.modifier == shader::Modifier::none;
}
bool identity(const shader::Operand &o) { return o.lanes == std::array<uint32_t,4>{0,1,2,3}; }
struct Pattern { uint32_t color, uv; bool opaque; };
Pattern pattern(const shader::Program &p) {
    using namespace shader;
    require(p.stage == Stage::pixel && (p.instructions.size() == 2 || p.instructions.size() == 3) && p.outputs[0] == 15 && p.position_input == 16);
    const auto &sample = p.instructions[0], &multiply = p.instructions.back();
    require(sample.opcode == Opcode::sample && sample.source_count == 3 && !sample.saturate &&
        sample.destination.kind == Register::temporary && sample.destination.mask == 15 &&
        plain(sample.destination, Register::temporary, sample.destination.index) &&
        sample.sources[0].kind == Register::input && plain(sample.sources[0], Register::input, sample.sources[0].index) &&
        sample.sources[0].lanes[0] == 0 && sample.sources[0].lanes[1] == 1 &&
        plain(sample.sources[1], Register::texture, 0) && identity(sample.sources[1]) && plain(sample.sources[2], Register::sampler, 0));
    const auto temporary = sample.destination.index;
    if(p.instructions.size()==3) {
        const auto &alpha=p.instructions[1];
        require(alpha.opcode==Opcode::move && alpha.source_count==1 && !alpha.saturate &&
            plain(alpha.destination,Register::temporary,temporary) && alpha.destination.mask==8 &&
            alpha.sources[0].kind==Register::immediate && alpha.sources[0].modifier==Modifier::none &&
            !alpha.sources[0].relative.enabled && alpha.sources[0].literal==std::array<uint32_t,4>{0x3f800000,0x3f800000,0x3f800000,0x3f800000});
    }
    require(multiply.opcode == Opcode::multiply && multiply.source_count == 2 && !multiply.saturate &&
        plain(multiply.destination, Register::output, 0) && multiply.destination.mask == 15 &&
        plain(multiply.sources[0], Register::temporary, temporary) && identity(multiply.sources[0]) &&
        multiply.sources[1].kind == Register::input && plain(multiply.sources[1], Register::input, multiply.sources[1].index) && identity(multiply.sources[1]));
    Pattern result{multiply.sources[1].index, sample.sources[0].index, p.instructions.size()==3};
    require(result.color < 16 && result.uv < 16 && result.color != result.uv && p.textures[0] && p.samplers[0]);
    for (uint32_t i = 0; i < 16; ++i) {
        require(p.inputs[i] == (i == result.color ? 15u : (i == result.uv ? 3u : 0u)));
        if (p.inputs[i]) require(p.interpolation[i] == 2); // linear, single sample, uniform clip w below
        if (i) require(!p.textures[i] && !p.samplers[i] && !p.outputs[i]);
    }
    for (const auto count : p.constants) require(!count);
    return result;
}
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
namespace op = ov::opset13;
using Node = std::shared_ptr<ov::Node>;
Node integer(int32_t v) { return op::Constant::create(ov::element::i32,ov::Shape{1,1},{v}); }
Node number(float v) { return op::Constant::create(ov::element::f32,ov::Shape{1,1},{v}); }
Node axes(std::initializer_list<int64_t> v) { return op::Constant::create(ov::element::i64,ov::Shape{v.size()},std::vector<int64_t>(v)); }
Node lane(const Node &v, int64_t c) { return std::make_shared<op::Gather>(v,axes({c}),op::Constant::create(ov::element::i32,ov::Shape{},{1})); }
Node add(const Node &a,const Node &b) { return std::make_shared<op::Add>(a,b); }
Node sub(const Node &a,const Node &b) { return std::make_shared<op::Subtract>(a,b); }
Node mul(const Node &a,const Node &b) { return std::make_shared<op::Multiply>(a,b); }
Node pred(const Node &v) { return std::make_shared<op::Convert>(v,ov::element::i32); }
Node floating(const Node &v) { return std::make_shared<op::Convert>(v,ov::element::f32); }
Node both(const Node &a,const Node &b) { return std::make_shared<op::BitwiseAnd>(a,b); }
}
struct LinearTextureDrawExecutor::State {
    shader::Program vs; Pattern pixel;
    uint32_t width,height,sw,sh,sf,rows,count; bool failed=false, point=false;
    shader::VertexExecutor vertex; ProjectionExecutor projection;
    ov::Core core; ov::CompiledModel compiled; ov::InferRequest request; std::string device;
    State(const shader::Program &v,const shader::Program &p,uint32_t w,uint32_t h,uint32_t format,
        uint32_t source_width,uint32_t source_height,uint32_t source_format,const std::string &backend,bool blend,shader::TextureFilter filter)
        : vs(v),pixel(pattern(p)),width(w),height(h),sw(source_width),sh(source_height),sf(source_format),
          rows(1),count(w),point(filter==shader::TextureFilter::point),vertex(v,6,backend),projection(6,backend) {
        require((filter==shader::TextureFilter::point || filter==shader::TextureFilter::linear) && (!pixel.opaque || point));
        require(w && w<=4096 && h && h<=8192 && uint64_t(w)*h<=4*1024*1024 && sw && sw<=8192 && sh && sh<=8192 && uint64_t(sw)*sh<=4*1024*1024);
        require((format==28 || format==87) && (sf==28 || sf==87) && vs.stage==shader::Stage::vertex && vs.position_output<16 &&
            (vs.outputs[pixel.color]&15)==15 && (vs.outputs[pixel.uv]&3)==3);
        rows=(std::min)(h,4096/w);while(h%rows)--rows;count=w*rows;
        auto parameter=[](ov::element::Type type,ov::Shape shape,const char *name) {
            auto p=std::make_shared<op::Parameter>(type,shape);p->output(0).get_tensor().set_names({name});return p;
        };
        std::array<std::shared_ptr<op::Parameter>,4> source{},color{};
        ov::ParameterVector parameters;
        for(unsigned c=0;c<(pixel.opaque?3u:4u);++c) {
            source[c]=parameter(ov::element::f32,{size_t(sw)*sh,1},("source"+std::to_string(c)).c_str());
            parameters.push_back(source[c]);
        }
        for(unsigned c=0;c<4;++c) {
            color[c]=parameter(ov::element::f32,{1,1},("color"+std::to_string(c)).c_str());
            parameters.push_back(color[c]);
        }
        auto bg=parameter(ov::element::u8,{count,4},"background");
        auto uv=parameter(ov::element::f32,{1,4},"uv");
        auto bounds=parameter(ov::element::i32,{1,4},"bounds"),clip=parameter(ov::element::i32,{1,4},"scissor");
        auto origin=parameter(ov::element::f32,{1,1},"origin"),origin_i=parameter(ov::element::i32,{1,1},"origin_i");
        auto half=parameter(ov::element::f32,{1,1},"half");
        std::vector<int32_t> indices(count),xx(count),yy(count);std::vector<float> xf(count),yf(count);
        for(uint32_t n=0;n<count;++n){indices[n]=int32_t(n);xx[n]=int32_t(n%w)*256+128;yy[n]=int32_t(n/w)*256+128;xf[n]=float(n%w)+.5f;yf[n]=float(n/w)+.5f;}
        auto row=op::Constant::create(ov::element::i32,ov::Shape{count},indices);
        auto choose=[&](const Node &condition,const Node &yes,const Node &no)->Node {
            auto alternate=std::make_shared<op::Reshape>(pred(std::make_shared<op::LogicalNot>(std::make_shared<op::Broadcast>(condition,axes({count,1})))),axes({count}),false);
            auto index=add(row,mul(alternate,op::Constant::create(ov::element::i32,ov::Shape{},{int32_t(count)})));
            auto choices=std::make_shared<op::Concat>(ov::OutputVector{std::make_shared<op::Broadcast>(yes,axes({count,1})),std::make_shared<op::Broadcast>(no,axes({count,1}))},0);
            return std::make_shared<op::Gather>(choices,index,op::Constant::create(ov::element::i32,ov::Shape{},{0}));
        };
        auto coordinate=[&](int c,uint32_t extent)->Node {
            Node x=op::Constant::create(ov::element::f32,ov::Shape{count,1},c ? yf : xf);if(c)x=add(x,origin);
            // Direct3D interpolation setup uses snapped vertex positions, just
            // like coverage. Reconstruct them on the provider, not on the host.
            auto low=mul(floating(lane(bounds,c)),number(1.f/256));
            auto span=mul(floating(sub(lane(bounds,c+2),lane(bounds,c))),number(1.f/256));
            auto t=std::make_shared<op::Divide>(sub(x,low),span);
            auto value=add(lane(uv,c),mul(t,sub(lane(uv,c+2),lane(uv,c))));
            value=choose(std::make_shared<op::Less>(value,number(0)),number(0),choose(std::make_shared<op::Greater>(value,number(1)),number(1),value));
            value=sub(mul(value,number(float(extent))),half);
            return choose(std::make_shared<op::Less>(value,number(0)),number(0),choose(std::make_shared<op::Greater>(value,number(float(extent-1))),number(float(extent-1)),value));
        };
        auto x=coordinate(0,sw),y=coordinate(1,sh),ix=both(pred(x),integer(0x7fffffff)),iy=both(pred(y),integer(0x7fffffff));
        auto fetch=[&](unsigned channel,int dx,int dy)->Node {
            auto sx=std::make_shared<op::Minimum>(add(ix,integer(dx)),integer(int32_t(sw-1)));
            auto sy=std::make_shared<op::Minimum>(add(iy,integer(dy)),integer(int32_t(sh-1)));
            auto index=std::make_shared<op::Reshape>(add(sx,mul(sy,integer(int32_t(sw)))),axes({count}),false);
            return std::make_shared<op::Gather>(source[channel],index,op::Constant::create(ov::element::i32,ov::Shape{},{0}));
        };
        auto mix=[](const Node &a,const Node &b,const Node &t)->Node{return add(mul(a,sub(number(1),t)),mul(b,t));};
        auto tx=sub(x,floating(ix)),ty=sub(y,floating(iy));
        // Independent channel ports avoid the NPU graph's incorrect packed
        // channel selection. Host work only decodes packed bytes into float
        // channels; sampling and modulation remain selected-device operations.
        std::array<Node,4> sampled{};
        for(unsigned c=0;c<(pixel.opaque?3u:4u);++c)
            sampled[c]=point ? fetch(c,0,0) : mix(mix(fetch(c,0,0),fetch(c,1,0),tx),mix(fetch(c,0,1),fetch(c,1,1),tx),ty);
        Node alpha=pixel.opaque ? Node(color[3]) : mul(sampled[3],color[3]);
        auto px=op::Constant::create(ov::element::i32,ov::Shape{count,1},xx);
        auto py=add(op::Constant::create(ov::element::i32,ov::Shape{count,1},yy),mul(origin_i,integer(256)));
        auto inside=[&](const Node &r)->Node {
            return both(both(pred(std::make_shared<op::GreaterEqual>(px,lane(r,0))),pred(std::make_shared<op::Less>(px,lane(r,2)))),
                both(pred(std::make_shared<op::GreaterEqual>(py,lane(r,1))),pred(std::make_shared<op::Less>(py,lane(r,3)))));
        };
        auto covered=both(inside(bounds),inside(clip));
        ov::OutputVector channels;
        for(int c=0;c<4;++c){
            const int source_lane=format!=sf && c<3 ? 2-c : c;
            const int color_lane=format==87 && c<3 ? 2-c : c;
            auto bc=lane(bg,c);
            Node fg=pixel.opaque && c==3 ? Node(color[3]) : mul(sampled[source_lane],color[color_lane]);
            auto value=mul(fg,number(255));if(blend)value=add(value,mul(floating(bc),sub(number(1),alpha)));
            value=choose(std::make_shared<op::Less>(value,number(0)),number(0),choose(std::make_shared<op::Greater>(value,number(255)),number(255),value));
            auto base=both(pred(value),integer(255)),fraction=sub(value,floating(base));
            auto inc=std::make_shared<op::BitwiseOr>(pred(std::make_shared<op::Greater>(fraction,number(.5f))),both(pred(std::make_shared<op::Equal>(fraction,number(.5f))),both(base,integer(1))));
            channels.push_back(add(mul(add(base,inc),covered),mul(pred(bc),sub(integer(1),covered))));
        }
        auto output=std::make_shared<op::Convert>(std::make_shared<op::Concat>(channels,1),ov::element::u8);
        parameters.insert(parameters.end(),{bg,uv,bounds,clip,origin,origin_i,half});
        auto model=std::make_shared<ov::Model>(ov::OutputVector{output,std::make_shared<op::Convert>(covered,ov::element::u8)},parameters);
        ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        if(backend=="NPU"){
            for(const auto &node:model->get_ops())node->get_rt_info()["precise_0"]=std::string{};
            std::ostringstream xml,weights;ov::pass::Serialize serializer(xml,weights);serializer.run_on_model(model);
            const auto bytes=weights.str();ov::Tensor data(ov::element::u8,ov::Shape{bytes.size()});if(!bytes.empty())std::memcpy(data.data(),bytes.data(),bytes.size());
            model=core.read_model(xml.str(),data);for(const auto &node:model->get_ops())require(node->get_rt_info().count("precise_0")!=0);
            properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        compiled=nr::provider::compile_model(core,model,backend,properties);const auto devices=compiled.get_property(ov::execution_devices);
        require(devices.size()==1 && (devices[0]==backend || devices[0].rfind(backend+".",0)==0));device=devices[0];request=compiled.create_infer_request();
    }
};
void LinearTextureDrawExecutor::validate_shader(const shader::Program &p){(void)pattern(p);}
LinearTextureDrawExecutor::LinearTextureDrawExecutor(const shader::Program &v,const shader::Program &p,uint32_t w,uint32_t h,uint32_t f,uint32_t sw,uint32_t sh,uint32_t sf,const std::string &b,bool blend,shader::TextureFilter filter)
    :state(std::make_unique<State>(v,p,w,h,f,sw,sh,sf,b,blend,filter)){}
LinearTextureDrawExecutor::~LinearTextureDrawExecutor()=default;
UniformDrawExecutor::Result LinearTextureDrawExecutor::execute(const std::vector<shader::VertexRegisters> &inputs,const shader::VertexConstants &constants,
    const Viewport &viewport,const PackedTexture &source,const std::vector<PackedColor> &background,const std::optional<std::array<int32_t,4>> &scissor){
    auto &s=*state;const auto start=Clock::now();
    require(!s.failed && inputs.size()==6 && source.width==s.sw && source.height==s.sh && source.format==s.sf && source.pixels.size()==size_t(s.sw)*s.sh &&
        background.size()==size_t(s.width)*s.height && viewport.x==0 && viewport.y==0 && viewport.width==float(s.width) && viewport.height==float(s.height));
    auto clip=scissor.value_or(std::array<int32_t,4>{0,0,int32_t(s.width),int32_t(s.height)});
    require(clip[0]>=0 && clip[1]>=0 && clip[0]<=clip[2] && clip[1]<=clip[3] && clip[2]<=int32_t(s.width) && clip[3]<=int32_t(s.height));
    auto began=Clock::now();auto shaded=s.vertex.execute(inputs,constants);UniformDrawExecutor::Result result{};
    result.execution_device=shaded.execution_device;result.timings.shader_ms=elapsed(began);require(result.execution_device==s.device);
    result.input_bytes=shaded.input_bytes;result.output_bytes=shaded.output_bytes;++result.provider_requests;
    std::vector<ClipPosition> positions(6);
    const auto color_bits=shaded.vertices[0][s.pixel.color];std::array<float,4> color{};
    std::memcpy(color.data(),color_bits.data(),16);
    for(const auto v:color)require(std::isfinite(v) && v>=0 && v<=1);
    for(size_t n=0;n<6;++n){
        require(shaded.vertices[n][s.pixel.color]==color_bits);
        std::memcpy(positions[n].data(),shaded.vertices[n][s.vs.position_output].data(),16);require(positions[n][3]==1.f);
    }
    began=Clock::now();const auto projected=s.projection.execute(positions,viewport);result.timings.projection_ms=elapsed(began);require(projected.execution_device==s.device);
    result.input_bytes+=6*16+24;result.output_bytes+=6*24+2*16;++result.provider_requests;
    const auto &a=projected.vertices[0],&b=projected.vertices[2];
    const std::array<float,4> rect{a.screen[0],a.screen[1],b.screen[0],b.screen[1]};
    const std::array<int32_t,4> bounds{a.snapped[0],a.snapped[1],b.snapped[0],b.snapped[1]};
    require(rect[0]<rect[2] && rect[1]<rect[3] && bounds[0]<bounds[2] && bounds[1]<bounds[3]);
    std::array<float,4> uv{};for(unsigned c=0;c<2;++c){std::memcpy(&uv[c],&shaded.vertices[0][s.pixel.uv][c],4);std::memcpy(&uv[c+2],&shaded.vertices[2][s.pixel.uv][c],4);}
    for(auto v:uv)require(std::isfinite(v) && std::abs(v)<=16);
    const uint32_t orders[2][6]{{0,1,2,2,3,0},{0,1,2,0,2,3}};bool accepted=false;
    for(const auto &order:orders){bool matches=true;for(size_t n=0;n<6;++n){const auto corner=order[n];const bool right=corner==1 || corner==2,bottom=corner>=2;
        const std::array<float,2> expected{rect[right?2:0],rect[bottom?3:1]};const Point snapped{bounds[right?2:0],bounds[bottom?3:1]};
        matches &= projected.vertices[n].screen==expected && projected.vertices[n].snapped==snapped;
        float u=0,v=0;std::memcpy(&u,&shaded.vertices[n][s.pixel.uv][0],4);std::memcpy(&v,&shaded.vertices[n][s.pixel.uv][1],4);
        matches &= u==uv[right?2:0] && v==uv[bottom?3:1];}accepted |= matches;}
    require(accepted);
    // The provider has already projected and bounded both triangles. Schedule
    // only row blocks intersecting those bounds and the scissor; the graph
    // still decides exact sample coverage, sampling and blending.
    const auto regions=bounded_triangle_regions(projected.triangle_bounds,clip,s.count);
    uint32_t first_row=s.height,end_row=0;
    for(const auto &region:regions)if(region[0]<region[2] && region[1]<region[3]){
        first_row=(std::min)(first_row,uint32_t(region[1]));end_row=(std::max)(end_row,uint32_t(region[3]));
    }
    result.pixels=background;
    if(first_row>=end_row){
        result.timings.total_ms=elapsed(start);result.used_rectangle=true;result.vertices=std::move(shaded.vertices);return result;
    }
    first_row-=first_row%s.rows;
    for(auto &v:clip)v*=256;
    s.failed=true;began=Clock::now();
    for(unsigned c=0;c<(s.pixel.opaque?3u:4u);++c) {
        auto *texels=s.request.get_tensor("source"+std::to_string(c)).data<float>();
        for(size_t n=0;n<source.pixels.size();++n)texels[n]=float(source.pixels[n][c])/255;
    }
    for(unsigned c=0;c<4;++c)s.request.get_tensor("color"+std::to_string(c)).data<float>()[0]=color[c];
    std::memcpy(s.request.get_tensor("uv").data<float>(),uv.data(),16);
    std::memcpy(s.request.get_tensor("bounds").data<int32_t>(),bounds.data(),16);std::memcpy(s.request.get_tensor("scissor").data<int32_t>(),clip.data(),16);
    s.request.get_tensor("half").data<float>()[0]=s.point?0.f:.5f;
    for(uint32_t y=first_row;y<end_row;y+=s.rows){
        const auto offset=size_t(y)*s.width;s.request.get_tensor("origin").data<float>()[0]=float(y);s.request.get_tensor("origin_i").data<int32_t>()[0]=int32_t(y);
        std::memcpy(s.request.get_tensor("background").data<uint8_t>(),background.data()+offset,size_t(s.count)*4);s.request.infer();
        const auto output=s.request.get_output_tensor(0),coverage=s.request.get_output_tensor(1);
        require(output.get_element_type()==ov::element::u8 && output.get_byte_size()==size_t(s.count)*4 && coverage.get_element_type()==ov::element::u8 && coverage.get_size()==s.count);
        std::memcpy(result.pixels.data()+offset,output.data<const uint8_t>(),size_t(s.count)*4);
        for(size_t n=0;n<s.count;++n){const auto value=coverage.data<const uint8_t>()[n];require(value<=1);result.covered_samples+=value;}
        result.input_bytes+=uint64_t(s.sw)*s.sh*(s.pixel.opaque?12u:16u)+uint64_t(s.count)*4+76;result.output_bytes+=uint64_t(s.count)*5;++result.provider_requests;++result.tile_executions;
    }
    result.timings.merger_ms=elapsed(began);result.timings.total_ms=elapsed(start);result.used_rectangle=true;result.vertices=std::move(shaded.vertices);s.failed=false;return result;
}
}
