// Local macOS test adapter only. The Windows prototype keeps BCrypt unchanged.
#include "memory-cache-v1/native/windows/registered_lab/provider_cache.hpp"
#include <openssl/evp.h>
namespace nr::provider {
CacheDigest::CacheDigest(){
    hash=EVP_MD_CTX_new();algorithm=const_cast<EVP_MD *>(EVP_sha256());
    if(!hash || EVP_DigestInit_ex(static_cast<EVP_MD_CTX *>(hash),static_cast<const EVP_MD *>(algorithm),nullptr)!=1){
        EVP_MD_CTX_free(static_cast<EVP_MD_CTX *>(hash));hash=nullptr;
        throw std::runtime_error("cannot initialize local SHA-256 adapter");
    }
}
CacheDigest::~CacheDigest(){EVP_MD_CTX_free(static_cast<EVP_MD_CTX *>(hash));}
void CacheDigest::bytes(const void *input,size_t size){
    if(EVP_DigestUpdate(static_cast<EVP_MD_CTX *>(hash),input,size)!=1)
        throw std::runtime_error("cannot hash local test content");
}
void CacheDigest::field(const std::string &value){
    const uint64_t size=value.size();std::array<unsigned char,8> length{};
    for(unsigned i=0;i<8;++i)length[i]=static_cast<unsigned char>(size>>(8*i));
    bytes(length.data(),length.size());bytes(value.data(),value.size());
}
std::string CacheDigest::finish(){
    std::array<unsigned char,32> digest{};unsigned size=0;
    if(EVP_DigestFinal_ex(static_cast<EVP_MD_CTX *>(hash),digest.data(),&size)!=1 || size!=digest.size())
        throw std::runtime_error("cannot finish local test digest");
    const char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(const auto b:digest){result+=hex[b>>4];result+=hex[b&15];}return result;
}
}
