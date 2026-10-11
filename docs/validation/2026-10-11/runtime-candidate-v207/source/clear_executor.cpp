#include "clear_executor.hpp"
#include "provider_compile.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
namespace nr::raster {
namespace {
namespace op=ov::opset13;
void require(bool value){if(!value)throw std::invalid_argument("unsupported color clear");}
}
struct ClearExecutor::State {
    uint32_t width,height;
    ov::Core core;ov::CompiledModel compiled;ov::InferRequest request;
    std::string execution_device;bool failed=false;
    State(uint32_t w,uint32_t h,uint32_t format,const std::string &backend):width(w),height(h){
        require(w && w<=8192 && h && h<=8192 && uint64_t(w)*h<=4194304 && (format==28 || format==87));
        require(backend=="CPU" || backend=="GPU" || backend=="NPU");
        const auto integer=[](int32_t x){return op::Constant::create(ov::element::i32,ov::Shape{},{x});};
        const auto number=[](float x){return op::Constant::create(ov::element::f32,ov::Shape{},{x});};
        const auto pred=[](const std::shared_ptr<ov::Node> &n){return std::make_shared<op::Convert>(n,ov::element::i32);};
        auto color=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,1,1,4});
        auto value=std::make_shared<op::Multiply>(std::make_shared<op::Clamp>(color,0.,1.),number(255));
        // Keep the integer value distinct: omitting this mask lets the tested
        // NPU compiler remove the fractional rounding increment.
        auto base=std::make_shared<op::BitwiseAnd>(pred(value),integer(255));
        auto fraction=std::make_shared<op::Subtract>(value,std::make_shared<op::Convert>(base,ov::element::f32));
        auto increment=std::make_shared<op::BitwiseOr>(pred(std::make_shared<op::Greater>(fraction,number(.5f))),
            std::make_shared<op::BitwiseAnd>(pred(std::make_shared<op::Equal>(fraction,number(.5f))),std::make_shared<op::BitwiseAnd>(base,integer(1))));
        std::shared_ptr<ov::Node> quantized=std::make_shared<op::Add>(base,increment);
        if(format==87)quantized=std::make_shared<op::Gather>(quantized,
            op::Constant::create(ov::element::i64,ov::Shape{4},{2,1,0,3}),integer(3));
        auto output=std::make_shared<op::Convert>(std::make_shared<op::Broadcast>(quantized,
            op::Constant::create<int64_t>(ov::element::i64,ov::Shape{4},{1,int64_t(h),int64_t(w),4})),ov::element::u8);
        auto model=std::make_shared<ov::Model>(ov::OutputVector{output},ov::ParameterVector{color});
        ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        if(backend=="CPU")properties.emplace(ov::hint::inference_precision(ov::element::f32));
        if(backend=="NPU"){
            for(const auto &node:model->get_ops())node->get_rt_info()["precise_0"]=std::string{};
            std::ostringstream xml,weights;ov::pass::Serialize serializer(xml,weights);serializer.run_on_model(model);
            const auto bytes=weights.str();ov::Tensor data(ov::element::u8,ov::Shape{bytes.size()});
            if(!bytes.empty())std::memcpy(data.data(),bytes.data(),bytes.size());
            model=core.read_model(xml.str(),data);
            for(const auto &node:model->get_ops())require(nr::provider::precision_preserved(node));
            properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        compiled=nr::provider::compile_model(core,model,backend,properties);
        const auto devices=compiled.get_property(ov::execution_devices);
        require(devices.size()==1 && (devices[0]==backend || devices[0].rfind(backend+".",0)==0));
        execution_device=devices[0];request=compiled.create_infer_request();
    }
};
ClearExecutor::ClearExecutor(uint32_t w,uint32_t h,uint32_t format,const std::string &backend):state(std::make_unique<State>(w,h,format,backend)){}
ClearExecutor::~ClearExecutor()=default;
ClearExecutor::Result ClearExecutor::execute(const Color &color){
    auto &s=*state;require(!s.failed);for(const auto value:color)require(std::isfinite(value));
    const auto started=std::chrono::steady_clock::now();s.failed=true;
    std::memcpy(s.request.get_input_tensor().data(),color.data(),sizeof(color));s.request.infer();
    const auto output=s.request.get_output_tensor();require(output.get_element_type()==ov::element::u8 && output.get_byte_size()==uint64_t(s.width)*s.height*4);
    Result result{std::vector<PackedColor>(size_t(s.width)*s.height),s.execution_device,0};
    std::memcpy(result.pixels.data(),output.data(),output.get_byte_size());
    result.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();s.failed=false;return result;
}
}
