// Keep Windows SDK macros out of the shader graph translation units.
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "provider_cache.hpp"
namespace nr::provider {
CacheDigest::CacheDigest(){
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw std::runtime_error("cannot initialize cache digest");
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0){
        BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;
        throw std::runtime_error("cannot create cache digest");
    }
}
CacheDigest::~CacheDigest(){
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
}
void CacheDigest::bytes(const void *input,size_t size){
    auto data=static_cast<const unsigned char *>(input);
    while(size){const auto n=ULONG((std::min)(size,size_t(1)<<26));
        if(BCryptHashData(hash,const_cast<PUCHAR>(data),n,0)<0)throw std::runtime_error("cannot hash cache content");
        data+=n;size-=n;
    }
}
void CacheDigest::field(const std::string &value){
    const uint64_t size=value.size();std::array<unsigned char,8> length{};
    for(unsigned i=0;i<8;++i)length[i]=static_cast<unsigned char>(size>>(8*i));
    bytes(length.data(),length.size());bytes(value.data(),value.size());
}
std::string CacheDigest::finish(){
    std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)<0)throw std::runtime_error("cannot finish cache digest");
    const char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(const auto b:digest){result+=hex[b>>4];result+=hex[b&15];}return result;
}
}
