#include "provider_cache.hpp"
#include <openvino/opsets/opset13.hpp>
#include <cstdio>
#include <cstring>
namespace {
void test(bool value){if(!value)throw std::runtime_error("provider cache identity contract failed");}
std::shared_ptr<ov::Model> graph(bool swap,uint32_t rows=1){
    namespace op=ov::opset13;
    auto input=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{rows,4});
    input->output(0).get_tensor().set_names({"input"});
    auto indices=op::Constant::create(ov::element::i64,ov::Shape{4},swap?std::vector<int64_t>{2,1,0,3}:std::vector<int64_t>{0,1,2,3});
    auto axis=op::Constant::create(ov::element::i64,ov::Shape{},{1});
    auto output=std::make_shared<op::Gather>(input,indices,axis);
    output->output(0).get_tensor().set_names({"output"});
    return std::make_shared<ov::Model>(ov::OutputVector{output},ov::ParameterVector{input});
}
std::pair<std::string,std::string> serialized(const std::shared_ptr<ov::Model> &model){
    std::ostringstream xml,weights;ov::pass::Serialize serializer(xml,weights);serializer.run_on_model(model);
    return {xml.str(),weights.str()};
}
}
int main()try{
    nr::provider::CacheDigest empty;
    test(empty.finish()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    nr::provider::CacheDigest abc;abc.bytes("abc",3);
    test(abc.finish()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const auto first=graph(false);const auto before=serialized(first);
    const auto identity=nr::provider::cache_model_identity(first);
    test(before==serialized(first));
    const auto changed=graph(true);
    test(identity!=nr::provider::cache_model_identity(changed));
    test(identity==nr::provider::cache_model_identity(graph(false)));
    test(identity!=nr::provider::cache_model_identity(graph(false,2)));
    auto renamed=graph(false);renamed->get_parameters()[0]->output(0).get_tensor().set_names({"different_input"});
    test(identity!=nr::provider::cache_model_identity(renamed));
    // IR round trips add generated tensor names. Their process-global suffixes
    // must not defeat cache reuse, while explicit binding names remain part of
    // the identity.
    ov::Core core;
    auto restored=[&](const std::shared_ptr<ov::Model> &model){
        const auto data=serialized(model);ov::Tensor weights(ov::element::u8,ov::Shape{data.second.size()});
        std::memcpy(weights.data(),data.second.data(),data.second.size());return core.read_model(data.first,weights);
    };
    test(nr::provider::cache_model_identity(restored(graph(false)))==nr::provider::cache_model_identity(restored(graph(false))));
    test(nr::provider::cache_model_identity(restored(graph(false)))!=nr::provider::cache_model_identity(restored(graph(true))));
    std::puts("{\"sha256_known_answers\":true,\"constant_permutation_isolated\":true,\"generated_names_stable\":true,\"source_model_unchanged\":true,\"shape_and_binding_names_isolated\":true,\"npu_execution_tested\":false}");
    return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
