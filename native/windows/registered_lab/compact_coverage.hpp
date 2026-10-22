#include "provider_infer_trace.hpp"
#pragma once
#include "raster_executor.hpp"
#include "provider_compile.hpp"
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <algorithm>
#include <cstring>
#include <sstream>
namespace nr::raster::compact {
struct NotApplicable : std::runtime_error {NotApplicable():std::runtime_error("compact coverage domain not applicable") {}};
inline void require(bool v){if(!v)throw std::invalid_argument("invalid compact coverage request");}
// Positive, exact per-axis power-of-two scaling and translation preserve all
// edge signs, winding, top-left ownership and degeneracy. Scissor comparisons
// still use the original integer point coordinates on the execution device.
inline bool normalize(const Sample &s,std::array<float,6> &v,std::array<float,2> &p){
    for(unsigned axis=0;axis<2;++axis){
        const int64_t origin=s.triangle[0][axis];std::array<int64_t,4> d{0,int64_t(s.triangle[1][axis])-origin,int64_t(s.triangle[2][axis])-origin,int64_t(s.point[axis])-origin};
        uint32_t bits=0;for(auto value:d)bits|=uint32_t(value);unsigned shift=0;
        if(bits)while(!(bits&1)){bits>>=1;++shift;}
        const int64_t divisor=int64_t(1)<<shift;for(auto &value:d)value/=divisor;
        const auto bounds=std::minmax_element(d.begin(),d.end());if(*bounds.second-*bounds.first>4095)return false;
        for(unsigned n=0;n<3;++n)v[n*2+axis]=float(d[n]);p[axis]=float(d[3]);
    }
    return true;
}
namespace graph {
namespace op=ov::opset13;using Node=std::shared_ptr<ov::Node>;
inline Node integer(int32_t x){return op::Constant::create(ov::element::i32,ov::Shape{}, {x});}
inline Node lane(const Node &v,int64_t i){return std::make_shared<op::Gather>(v,op::Constant::create(ov::element::i64,ov::Shape{1},{i}),integer(0));}
inline Node sub(const Node&a,const Node&b){return std::make_shared<op::Subtract>(a,b);}
inline Node mul(const Node&a,const Node&b){return std::make_shared<op::Multiply>(a,b);}
inline Node pred(const Node &v){return std::make_shared<op::Convert>(v,ov::element::i32);}
inline Node both(const Node&a,const Node&b){return std::make_shared<op::BitwiseAnd>(a,b);}
inline Node either(const Node&a,const Node&b){return std::make_shared<op::BitwiseOr>(a,b);}
inline Node positive(const Node &v){return pred(std::make_shared<op::Greater>(pred(v),integer(0)));}
inline Node negative(const Node &v){return pred(std::make_shared<op::Less>(pred(v),integer(0)));}
inline Node zero(const Node &v){return pred(std::make_shared<op::Equal>(pred(v),integer(0)));}
struct Sign {Node positive,negative,zero;};
inline Sign determinant(const Node &ax,const Node &by,const Node &ay,const Node &bx){
    // Each product is an exactly representable integer smaller than 2^24.
    // Their difference may round above 2^24, but its sign and zero status remain
    // exact. Conversion is safe because the magnitude is below 2^25.
    const auto value=sub(mul(ax,by),mul(ay,bx));return {positive(value),negative(value),zero(value)};
}
inline std::shared_ptr<ov::Model> model(uint32_t count,CullMode cull,bool ccw){
    require(count && count<=65536 && (cull==CullMode::none || cull==CullMode::front || cull==CullMode::back));
    auto vertices=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{6,count});auto points=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{2,count});auto original=std::make_shared<op::Parameter>(ov::element::i32,ov::Shape{2,count});auto clip=std::make_shared<op::Parameter>(ov::element::i32,ov::Shape{4,1});
    vertices->output(0).set_names({"vertices"});points->output(0).set_names({"points"});original->output(0).set_names({"original_points"});clip->output(0).set_names({"scissor"});
    std::array<Node,6> v;for(int64_t i=0;i<6;++i)v[size_t(i)]=lane(vertices,i);const auto px=lane(points,0),py=lane(points,1);
    const auto area=determinant(sub(v[2],v[0]),sub(v[5],v[1]),sub(v[3],v[1]),sub(v[4],v[0]));auto covered=either(area.positive,area.negative);
    if(cull!=CullMode::none)covered=((cull==CullMode::back)!=ccw)?area.positive:area.negative;
    covered=both(covered,pred(std::make_shared<op::GreaterEqual>(lane(original,0),lane(clip,0))));covered=both(covered,pred(std::make_shared<op::GreaterEqual>(lane(original,1),lane(clip,1))));
    covered=both(covered,pred(std::make_shared<op::Less>(lane(original,0),lane(clip,2))));covered=both(covered,pred(std::make_shared<op::Less>(lane(original,1),lane(clip,3))));
    for(size_t a=0;a<6;a+=2){const auto b=(a+2)%6;const auto dx=sub(v[b],v[a]),dy=sub(v[b+1],v[a+1]);const auto e=determinant(dx,sub(py,v[a+1]),dy,sub(px,v[a]));const auto top=either(negative(dy),both(zero(dy),positive(dx))),reverse=either(positive(dy),both(zero(dy),negative(dx)));
        covered=both(covered,either(both(area.positive,either(e.positive,both(e.zero,top))),both(area.negative,either(e.negative,both(e.zero,reverse)))));
    }
    return std::make_shared<ov::Model>(ov::OutputVector{covered},ov::ParameterVector{vertices,points,original,clip});
}
}
class Executor {
    uint32_t count;ov::Core core;ov::CompiledModel compiled;ov::InferRequest request;std::string device;bool failed=false;
public:
    Executor(uint32_t n,const std::string &backend,CullMode cull=CullMode::none,bool ccw=false):count(n){
        require(backend=="CPU" || backend=="NPU" || backend=="GPU");auto model=graph::model(count,cull,ccw);ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        if(backend=="NPU"){
            for(const auto &node:model->get_ops())node->get_rt_info()["precise_0"]=std::string{};
            std::ostringstream xml,weights;ov::pass::Serialize serializer(xml,weights);serializer.run_on_model(model);const auto bytes=weights.str();ov::Tensor data(ov::element::u8,ov::Shape{bytes.size()});if(!bytes.empty())std::memcpy(data.data(),bytes.data(),bytes.size());model=core.read_model(xml.str(),data);
            for(const auto &node:model->get_ops())require(nr::provider::precision_preserved(node));properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        compiled=nr::provider::compile_model(core,model,backend,properties);const auto devices=compiled.get_property(ov::execution_devices);require(devices.size()==1 && (devices[0]==backend || devices[0].rfind(backend+".",0)==0));device=devices[0];request=compiled.create_infer_request();
    }
    CoverageExecutor::Result execute(const std::vector<Sample> &samples,const std::array<int32_t,4> &clip){
        require(!failed && samples.size()==count && clip[0]<=clip[2] && clip[1]<=clip[3]);for(auto x:clip)require(x>=-coordinate_limit && x<=coordinate_limit+1);
        std::vector<float> v(size_t(count)*6),p(size_t(count)*2);std::vector<int32_t> original(size_t(count)*2);
        for(size_t i=0;i<count;++i){const auto &s=samples[i];for(const auto &point:s.triangle)for(auto x:point)require(x>=-coordinate_limit && x<=coordinate_limit);for(auto x:s.point)require(x>=-coordinate_limit && x<=coordinate_limit);std::array<float,6> compact_v;std::array<float,2> compact_p;if(!normalize(s,compact_v,compact_p))throw NotApplicable();for(size_t c=0;c<6;++c)v[c*count+i]=compact_v[c];for(size_t c=0;c<2;++c){p[c*count+i]=compact_p[c];original[c*count+i]=s.point[c];}}
        failed=true;std::memcpy(request.get_tensor("vertices").data<float>(),v.data(),size_t(count)*24);std::memcpy(request.get_tensor("points").data<float>(),p.data(),size_t(count)*8);std::memcpy(request.get_tensor("original_points").data<int32_t>(),original.data(),size_t(count)*8);std::memcpy(request.get_tensor("scissor").data<int32_t>(),clip.data(),16);
        nr::provider::traced_infer(request,__FILE__,__LINE__);const auto output=request.get_output_tensor();require(output.get_element_type()==ov::element::i32 && output.get_size()==count);CoverageExecutor::Result result{{},device};result.covered.resize(count);for(size_t i=0;i<count;++i){const auto x=output.data<const int32_t>()[i];require(x==0 || x==1);result.covered[i]=uint8_t(x);}failed=false;return result;
    }
};
}
