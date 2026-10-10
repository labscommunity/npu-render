#include "color_interpolator.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace nr::raster {
namespace {
namespace op = ov::opset13;
using Node = std::shared_ptr<ov::Node>;
void require(bool v) { if(!v) throw std::invalid_argument("unsupported color interpolation"); }
Node integer(int32_t v) { return op::Constant::create(ov::element::i32,ov::Shape{},{v}); }
Node number(float v) { return op::Constant::create(ov::element::f32,ov::Shape{},{v}); }
Node lane(const Node &v,int64_t i) { return std::make_shared<op::Gather>(v,op::Constant::create(ov::element::i64,ov::Shape{1},{i}),integer(1)); }
Node add(const Node &a,const Node &b) { return std::make_shared<op::Add>(a,b); }
Node sub(const Node &a,const Node &b) { return std::make_shared<op::Subtract>(a,b); }
Node mul(const Node &a,const Node &b) { return std::make_shared<op::Multiply>(a,b); }
Node floating(const Node &a) { return std::make_shared<op::Convert>(a,ov::element::f32); }
Node safe_denominator(const Node &a) { return add(a,floating(std::make_shared<op::Equal>(a,number(0)))); }
}
struct ColorInterpolator::State {
    uint32_t count;
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    std::string device;
    bool failed=false;
    State(uint32_t n,const std::string &backend,bool perspective):count(n) {
        require(n && n<=65536 && (backend=="NPU" || backend=="CPU" || backend=="GPU"));
        auto vertices=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,9});
        auto colors=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,12});
        auto points=std::make_shared<op::Parameter>(ov::element::i32,ov::Shape{n,2});
        auto coverage=std::make_shared<op::Parameter>(ov::element::i32,ov::Shape{n,1});
        vertices->output(0).get_tensor().set_names({"vertices"});colors->output(0).get_tensor().set_names({"colors"});
        points->output(0).get_tensor().set_names({"points"});coverage->output(0).get_tensor().set_names({"coverage"});
        auto px=mul(floating(lane(points,0)),number(1.f/256)),py=mul(floating(lane(points,1)),number(1.f/256));
        auto edge=[&](unsigned a,unsigned b,const Node &x,const Node &y) {
            return sub(mul(sub(lane(vertices,b*3),lane(vertices,a*3)),sub(y,lane(vertices,a*3+1))),
                       mul(sub(lane(vertices,b*3+1),lane(vertices,a*3+1)),sub(x,lane(vertices,a*3))));
        };
        std::array<Node,3> weight;
        for(unsigned v=0;v<3;++v) {
            weight[v]=edge((v+1)%3,(v+2)%3,px,py);
            if(perspective)weight[v]=mul(weight[v],lane(vertices,v*3+2));
        }
        auto denominator=safe_denominator(add(add(weight[0],weight[1]),weight[2]));
        ov::OutputVector channels;
        for(unsigned c=0;c<4;++c) {
            auto sum=add(add(mul(weight[0],lane(colors,c)),mul(weight[1],lane(colors,4+c))),mul(weight[2],lane(colors,8+c)));
            channels.push_back(mul(std::make_shared<op::Divide>(sum,denominator),floating(coverage)));
        }
        auto model=std::make_shared<ov::Model>(ov::OutputVector{std::make_shared<op::Concat>(channels,1)},ov::ParameterVector{vertices,colors,points,coverage});
        ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        if(backend=="NPU") {
            for(const auto &node:model->get_ops())node->get_rt_info()["precise_0"]=std::string{};
            std::ostringstream xml,weights;ov::pass::Serialize serializer(xml,weights);serializer.run_on_model(model);
            const auto bytes=weights.str();ov::Tensor data(ov::element::u8,ov::Shape{bytes.size()});
            if(!bytes.empty())std::memcpy(data.data(),bytes.data(),bytes.size());
            model=core.read_model(xml.str(),data);
            for(const auto &node:model->get_ops())if(!node->get_rt_info().count("precise_0"))throw std::runtime_error("interpolation precision marker lost");
            properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        compiled=core.compile_model(model,backend,properties);
        const auto devices=compiled.get_property(ov::execution_devices);
        require(devices.size()==1 && (devices[0]==backend || devices[0].rfind(backend+".",0)==0));
        device=devices[0];request=compiled.create_infer_request();
    }
};
ColorInterpolator::ColorInterpolator(uint32_t n,const std::string &backend,bool perspective):state(std::make_unique<State>(n,backend,perspective)) {}
ColorInterpolator::~ColorInterpolator()=default;
ColorInterpolator::Result ColorInterpolator::execute(const std::array<ProjectedVertex,3> &triangle,const std::array<Color,3> &colors,
    const std::vector<Point> &points,const std::vector<uint8_t> &coverage) {
    auto &s=*state;require(!s.failed && points.size()==s.count && coverage.size()==s.count);
    std::array<float,9> vertices{};
    for(unsigned v=0;v<3;++v) {
        for(const auto f:triangle[v].screen)require(std::isfinite(f) && std::abs(f)<=16384);
        require(std::isfinite(triangle[v].inverse_w) && triangle[v].inverse_w>0 && triangle[v].inverse_w<=65536);
        for(const auto f:colors[v])require(std::isfinite(f) && std::abs(f)<=16384);
        vertices[v*3]=triangle[v].screen[0];vertices[v*3+1]=triangle[v].screen[1];vertices[v*3+2]=triangle[v].inverse_w;
    }
    for(size_t i=0;i<points.size();++i) {
        require(coverage[i]<=1);
        for(const auto p:points[i])require(p>=-coordinate_limit && p<=coordinate_limit);
    }
    s.failed=true;
    std::memcpy(s.request.get_tensor("vertices").data<float>(),vertices.data(),sizeof(vertices));
    std::memcpy(s.request.get_tensor("colors").data<float>(),colors.data(),sizeof(colors));
    std::memcpy(s.request.get_tensor("points").data<int32_t>(),points.data(),points.size()*sizeof(Point));
    auto c=s.request.get_tensor("coverage");for(size_t i=0;i<coverage.size();++i)c.data<int32_t>()[i]=coverage[i];
    s.request.infer();const auto output=s.request.get_output_tensor();
    require(output.get_element_type()==ov::element::f32 && output.get_size()==size_t(s.count)*4);
    Result result{{},s.device};result.colors.resize(s.count);
    std::memcpy(result.colors.data(),output.data<const float>(),result.colors.size()*sizeof(Color));
    for(const auto &color:result.colors)for(const auto f:color)require(std::isfinite(f));
    s.failed=false;return result;
}
}
