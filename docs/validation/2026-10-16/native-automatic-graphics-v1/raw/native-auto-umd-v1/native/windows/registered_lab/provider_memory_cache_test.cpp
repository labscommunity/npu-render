#include "provider_memory_cache.hpp"
#include <openvino/opsets/opset13.hpp>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
void require(bool value){if(!value)throw std::runtime_error("compiled memory cache contract failed");}
std::shared_ptr<ov::Model> graph(float add=1.f,const std::string &input_name="input",const std::string &output_name="output",size_t count=64){
    namespace op=ov::opset13;
    auto input=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,count});
    input->output(0).get_tensor().set_names({input_name});
    auto output=std::make_shared<op::Add>(input,op::Constant::create(ov::element::f32,ov::Shape{},{add}));
    output->output(0).get_tensor().set_names({output_name});
    return std::make_shared<ov::Model>(ov::OutputVector{output},ov::ParameterVector{input});
}
uint64_t verified=0;
void fill(ov::InferRequest &request,float offset){
    auto tensor=request.get_tensor("input");require(tensor.get_size()==64);
    for(size_t i=0;i<64;++i)tensor.data<float>()[i]=float(i)+offset;
}
void check(ov::InferRequest &request,float offset,float add){
    request.infer();const auto tensor=request.get_tensor("output");require(tensor.get_size()==64);
    for(size_t i=0;i<64;++i){require(tensor.data<const float>()[i]==float(i)+offset+add);++verified;}
}
}
int main(int argc,char **argv)try{
    if(argc>2)throw std::invalid_argument("expected optional CPU or NPU backend");
    const std::string backend=argc==2?argv[1]:"CPU";
    if(backend!="CPU" && backend!="NPU")throw std::invalid_argument("expected CPU or NPU backend");
    const ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY),ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY)};
    auto identity=[&](const std::shared_ptr<ov::Model> &model,const ov::AnyMap &p){return nr::provider::compiled_memory_identity(model,backend,p,"");};
    const auto original=graph();const auto key=identity(original,properties);
    require(key==identity(graph(),properties));
    require(key!=identity(graph(2.f),properties));
    require(key!=identity(graph(1.f,"different_input"),properties));
    require(key!=identity(graph(1.f,"input","different_output"),properties));
    require(key!=identity(graph(1.f,"input","output",32),properties));
    auto profile=properties;profile[ov::enable_profiling.name()]=true;require(key!=identity(graph(),profile));
    auto precision=properties;precision[ov::hint::execution_mode.name()]=ov::hint::ExecutionMode::PERFORMANCE;require(key!=identity(graph(),precision));
    auto string_value=properties;string_value["TEST_KEY"]=std::string("true");auto bool_value=properties;bool_value["TEST_KEY"]=true;
    require(identity(graph(),string_value)!=identity(graph(),bool_value));
    require(key!=nr::provider::compiled_memory_identity(original,backend,properties,"different-directory"));
    bool rejected=false;auto unsupported=properties;unsupported["TEST_KEY"]=std::vector<int>{1,2};
    try{(void)identity(graph(),unsupported);}catch(const std::invalid_argument &){rejected=true;}require(rejected);
    auto alias=graph();alias->output(0).get_tensor().set_names({"output","second_alias"});require(key!=identity(alias,properties));
    nr::provider::CompiledMemoryCache cache(2);size_t backend_calls=0;
    auto load=[&](ov::Core &core,const std::shared_ptr<ov::Model> &model){
        return cache.get(core,model,backend,properties,"",[&](){++backend_calls;require(nr::provider::compile_statistics().memory_evictions==(backend_calls>2?backend_calls-2:0));return core.compile_model(model,backend,properties);});
    };
    ov::InferRequest first,second;
    {ov::Core core;first=load(core,graph()).create_infer_request();require(backend_calls==1);}
    // The first caller Core has been destroyed. The cache owns its context.
    ov::Core core;second=load(core,graph()).create_infer_request();require(backend_calls==1);
    for(unsigned round=0;round<8;++round){
        const auto a=float(round*128),b=-float(round*128);
        fill(first,a);fill(second,b);check(first,a,1.f);check(second,b,1.f);
        fill(first,a+64.f);check(second,b,1.f);check(first,a+64.f,1.f);
    }
    auto aliased=load(core,alias).create_infer_request();require(backend_calls==2);
    fill(aliased,4.f);check(aliased,4.f,1.f);
    auto changed=load(core,graph(2.f)).create_infer_request();require(backend_calls==3 && cache.size()==2);
    fill(changed,5.f);check(changed,5.f,2.f);
    // Eviction releases only the cached reference. Existing requests survive.
    fill(first,9.f);check(first,9.f,1.f);
    auto reloaded=load(core,graph()).create_infer_request();require(backend_calls==4 && cache.size()==2);
    fill(reloaded,11.f);check(reloaded,11.f,1.f);
    auto hit=load(core,graph()).create_infer_request();require(backend_calls==4);
    fill(hit,13.f);check(hit,13.f,1.f);
    bool injected=false;
    try{(void)cache.get(core,graph(3.f),backend,properties,"",[&]()->ov::CompiledModel{require(nr::provider::compile_statistics().memory_evictions==3);injected=true;throw std::runtime_error("injected compile failure");});}
    catch(const std::runtime_error &e){require(std::strcmp(e.what(),"injected compile failure")==0);}
    require(injected && cache.size()==0);
    bool ran=false;rejected=false;
    try{(void)cache.get(core,graph(),backend,properties,"",[&](){ran=true;return core.compile_model(graph(),backend,properties);});}
    catch(const std::runtime_error &e){require(std::strcmp(e.what(),"compiled memory cache is poisoned")==0);rejected=true;}
    require(rejected && !ran);
    const auto stats=nr::provider::compile_statistics();
    require(stats.memory_requests==7 && stats.memory_hits==2 && stats.memory_evictions==3);
    std::printf("{\"execution_device\":\"%s\",\"passed\":true,\"verified_values\":%llu,\"backend_calls\":%llu,\"memory_hits\":%llu,\"evictions\":%llu,\"binding_identity_isolated\":true,\"properties_isolated\":true,\"independent_mutable_inputs\":true,\"creator_core_lifetime\":true,\"eviction_preserves_active_requests\":true,\"compile_failure_poisoned\":true}\n",backend.c_str(),static_cast<unsigned long long>(verified),static_cast<unsigned long long>(backend_calls),static_cast<unsigned long long>(stats.memory_hits),static_cast<unsigned long long>(stats.memory_evictions));
    return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
