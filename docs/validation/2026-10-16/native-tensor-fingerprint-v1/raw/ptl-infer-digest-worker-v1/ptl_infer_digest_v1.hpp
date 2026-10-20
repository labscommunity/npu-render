#pragma once
// Diagnostic-only fingerprints. Every request still executes on the NPU.
#include <openvino/openvino.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
namespace nr::provider {
class TensorDigestAlgorithm {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
public:
    TensorDigestAlgorithm(){
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            throw std::runtime_error("tensor digest algorithm unavailable");
    }
    ~TensorDigestAlgorithm(){if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    TensorDigestAlgorithm(const TensorDigestAlgorithm&)=delete;
    TensorDigestAlgorithm& operator=(const TensorDigestAlgorithm&)=delete;
    std::string hash(const void *bytes,size_t size){
        if(size>ULONG_MAX || (size&&!bytes))throw std::invalid_argument("tensor digest extent");
        std::array<unsigned char,32> output{};
        if(BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)),
                      ULONG(size),output.data(),ULONG(output.size()))<0)
            throw std::runtime_error("tensor digest failed");
        constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
        for(const auto byte:output){result.push_back(hex[byte>>4]);result.push_back(hex[byte&15]);}
        return result;
    }
};
inline void profile_tensor_request(ov::InferRequest &request,const char *component){
    using Clock=std::chrono::steady_clock;
    static thread_local TensorDigestAlgorithm algorithm;
    static thread_local bool qualified=false;
    if(!qualified){
        const std::array<unsigned char,3> abc{'a','b','c'};
        if(algorithm.hash(nullptr,0)!="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ||
           algorithm.hash(abc.data(),abc.size())!="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
            throw std::runtime_error("tensor digest known answers differ");
        std::printf("{\"ptl_tensor_digest_contract\":true,\"known_answers\":2,\"passed\":true}\n");
        std::fflush(stdout);qualified=true;
    }
    const auto input=request.get_input_tensor();
    if(input.get_byte_size()!=1572864 || input.get_shape()!=ov::Shape({6,131072}))
        throw std::invalid_argument("tensor digest input shape");
    const auto began=Clock::now();
    const auto input_hash=algorithm.hash(input.data(),input.get_byte_size());
    const auto issued=Clock::now();
    request.start_async();
    if(!request.wait_for(std::chrono::milliseconds(10000)))
        throw std::runtime_error("profiled PTL inference deadline");
    const auto finished=Clock::now();
    const auto output=request.get_output_tensor();
    if(output.get_byte_size()!=1572864 || output.get_shape()!=ov::Shape({6,131072}))
        throw std::invalid_argument("tensor digest output shape");
    const auto output_hash=algorithm.hash(output.data(),output.get_byte_size());
    const auto hashed=Clock::now();
    const auto wait=std::chrono::duration<double,std::milli>(finished-issued).count();
    const auto digest=std::chrono::duration<double,std::milli>((issued-began)+(hashed-finished)).count();
    std::printf("{\"ptl_tensor_digest\":true,\"component\":\"%s\",\"input_sha256\":\"%s\",\"output_sha256\":\"%s\",\"input_bytes\":1572864,\"output_bytes\":1572864,\"inference_wait_ms\":%.6f,\"digest_ms\":%.6f,\"npu_call_executed\":true,\"result_reused\":false}\n",
                component,input_hash.c_str(),output_hash.c_str(),wait,digest);
    std::fflush(stdout);
}
}
