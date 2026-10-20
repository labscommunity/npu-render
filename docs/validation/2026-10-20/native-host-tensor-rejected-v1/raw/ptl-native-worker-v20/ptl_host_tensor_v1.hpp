#pragma once
// CPU staging memory only. The existing compiled models still execute on NPU.
#include <openvino/openvino.hpp>
#include <windows.h>
#include <cstdio>
#include <stdexcept>
namespace nr::provider {
inline unsigned tensor_page_protection(const void *data){
    MEMORY_BASIC_INFORMATION information{};
    if(VirtualQuery(data,&information,sizeof(information))!=sizeof(information))
        throw std::runtime_error("tensor memory query failed");
    return information.Protect;
}
inline void bind_host_tensors(ov::InferRequest &request,const char *component){
    const auto old_input=request.get_input_tensor(),old_output=request.get_output_tensor();
    const ov::Shape shape{6,131072};
    if(old_input.get_shape()!=shape || old_output.get_shape()!=shape ||
       old_input.get_element_type()!=ov::element::f16 || old_output.get_element_type()!=ov::element::f16)
        throw std::invalid_argument("host tensor binding shape");
    ov::Tensor input(ov::element::f16,shape),output(ov::element::f16,shape);
    if(input.get_byte_size()!=1572864 || output.get_byte_size()!=1572864 ||
       !input.is_continuous() || !output.is_continuous())
        throw std::runtime_error("host tensor binding layout");
    request.set_input_tensor(input);request.set_output_tensor(output);
    if(request.get_input_tensor().data()!=input.data() || request.get_output_tensor().data()!=output.data())
        throw std::runtime_error("host tensor binding identity");
    const auto before_input=tensor_page_protection(old_input.data());
    const auto before_output=tensor_page_protection(old_output.data());
    const auto after_input=tensor_page_protection(input.data());
    const auto after_output=tensor_page_protection(output.data());
    if((after_input|after_output)&(PAGE_NOCACHE|PAGE_WRITECOMBINE))
        throw std::runtime_error("host staging memory is not cacheable");
    std::printf("{\"ptl_host_tensor_binding\":true,\"component\":\"%s\",\"bytes_per_tensor\":1572864,\"previous_input_protection\":%u,\"previous_output_protection\":%u,\"input_protection\":%u,\"output_protection\":%u,\"cpu_rendering\":false,\"result_cache\":false,\"plugin_managed_transfer\":true}\n",
                component,before_input,before_output,after_input,after_output);
    std::fflush(stdout);
}
}
