#include "provider_compile.hpp"
#include <openvino/opsets/opset13.hpp>
#include <cstdio>
#include <cstring>
namespace {
void test(bool value){if(!value)throw std::runtime_error("shared model contract failed");}
std::shared_ptr<ov::Model> graph(bool swap,const std::string &binding){
    namespace op=ov::opset13;
    auto input=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{16,4});
    input->set_friendly_name(binding);input->output(0).get_tensor().set_names({binding});
    auto indices=op::Constant::create(ov::element::i32,ov::Shape{4},swap?std::vector<int32_t>{2,1,0,3}:std::vector<int32_t>{0,1,2,3});
    auto output=std::make_shared<op::Gather>(input,indices,op::Constant::create(ov::element::i32,ov::Shape{},{1}));
    output->output(0).get_tensor().set_names({"output"});
    return std::make_shared<ov::Model>(ov::OutputVector{output},ov::ParameterVector{input});
}
void verify(ov::InferRequest &request,const std::string &binding,bool swap,float offset){
    auto tensor=request.get_tensor(binding);std::vector<float> data(64);
    for(size_t i=0;i<data.size();++i)data[i]=float(i)+offset;
    std::memcpy(tensor.data<float>(),data.data(),data.size()*sizeof(float));request.infer();
    const auto out=request.get_tensor("output");test(out.get_size()==64);
    for(size_t i=0;i<data.size();++i){const auto c=i%4;const auto from=swap && c<3?2-c:c;test(out.data<const float>()[i]==data[i-c+from]);}
}
}
int main(int argc,char **argv)try{
    const std::string backend=argc==2?argv[1]:"CPU";test(backend=="CPU" || backend=="NPU");
    test(!_putenv_s("NR_RENDER_REUSE_COMPILED","1"));
    const ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
    nr::provider::CompileOwner first_owner;ov::CompiledModel first;
    {ov::Core creator;first=nr::provider::compile_model(creator,graph(false,"input"),backend,properties,first_owner);}
    const auto devices=first.get_property(ov::execution_devices);test(devices.size()==1 && (devices[0]==backend || devices[0].rfind(backend+".",0)==0));
    ov::Core second_core;nr::provider::CompileOwner second_owner,swap_owner,renamed_owner;
    auto second=nr::provider::compile_model(second_core,graph(false,"input"),backend,properties,second_owner);
    if(backend=="NPU")test(first_owner && first_owner==second_owner);
    auto a=first.create_infer_request(),b=second.create_infer_request();
    test(a.get_tensor("input").data()!=b.get_tensor("input").data());
    verify(a,"input",false,0);verify(b,"input",false,100);verify(a,"input",false,200);
    auto swapped=nr::provider::compile_model(second_core,graph(true,"input"),backend,properties,swap_owner);
    auto renamed=nr::provider::compile_model(second_core,graph(false,"different_input"),backend,properties,renamed_owner);
    if(backend=="NPU")test(swap_owner!=second_owner && renamed_owner!=second_owner);
    auto c=swapped.create_infer_request(),d=renamed.create_infer_request();verify(c,"input",true,300);verify(d,"different_input",false,400);
    a=ov::InferRequest{};first=ov::CompiledModel{};first_owner.reset();verify(b,"input",false,500);
    std::weak_ptr<ov::CompiledModel> lifetime=second_owner;b=ov::InferRequest{};second=ov::CompiledModel{};second_owner.reset();
    if(backend=="NPU")test(lifetime.expired());
    std::printf("{\"execution_device\":\"%s\",\"independent_requests\":true,\"creator_core_released\":true,\"binding_names_isolated\":true,\"constant_permutations_isolated\":true,\"weak_lifetime\":true,\"live_model_hits\":%llu,\"passed\":true}\n",devices[0].c_str(),static_cast<unsigned long long>(nr::provider::live_model_hits.load()));
    return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
