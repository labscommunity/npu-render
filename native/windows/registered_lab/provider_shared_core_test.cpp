#include "provider_compile.hpp"
#include <openvino/opsets/opset13.hpp>
#include <cstdio>
namespace op=ov::opset13;
int main()try{
 if(!nr::provider::shared_core_experiment())throw std::runtime_error("shared-core experiment required");
 std::vector<ov::InferRequest> requests;uint64_t checked=0;
 for(size_t count:{16u,32u,64u,128u}){
  ov::Core caller;auto input=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,count});input->output(0).get_tensor().set_names({"input"});
  auto value=std::make_shared<op::Add>(input,op::Constant::create(ov::element::f32,ov::Shape{1},{1.f}));auto model=std::make_shared<ov::Model>(ov::OutputVector{value},ov::ParameterVector{input});
  auto compiled=nr::provider::compile_model(caller,model,"NPU",ov::AnyMap{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)});
  if(compiled.get_property(ov::execution_devices)!=std::vector<std::string>{"NPU"})throw std::runtime_error("device mismatch");
  requests.push_back(compiled.create_infer_request());requests.push_back(compiled.create_infer_request());
 }
 for(unsigned frame=0;frame<8;++frame)for(size_t index=0;index<requests.size();++index){
  auto &request=requests[index];auto input=request.get_tensor("input");for(size_t n=0;n<input.get_size();++n)input.data<float>()[n]=float(n+index+frame);
  request.infer();auto output=request.get_output_tensor();for(size_t n=0;n<output.get_size();++n){if(output.data<const float>()[n]!=float(n+index+frame+1))throw std::runtime_error("shared core result mismatch");++checked;}
 }
 std::printf("{\"passed\":true,\"execution_device\":\"NPU\",\"checked_values\":%llu,\"independent_requests\":8,\"provider_calls\":64,\"caller_core_lifetimes_ended\":true,\"hardware_context_count_measured\":false}\n",static_cast<unsigned long long>(checked));return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
