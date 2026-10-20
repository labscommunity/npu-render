#pragma once
#include "indexed_pipe.hpp"
#include "provider_cache.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <cstdlib>
#include <cstdio>
namespace nr::registered_lab {
// Optional, bounded local diagnostic capture. The caller creates a private
// directory. Recorded pixels stay on the test machine and never enter Git.
class RequestRecording {
    std::filesystem::path directory;uint64_t bytes=0;
    static constexpr uint64_t maximum=uint64_t(16)*1024*1024*1024;
    void append(const std::string &line){std::ofstream log(directory/"index.jsonl",std::ios::binary|std::ios::app);if(!log || !(log<<line<<'\n') || !log.flush())throw std::runtime_error("request recording index failed");}
    static std::string digest(const uint8_t *data,size_t size){nr::provider::CacheDigest value;value.bytes(data,size);return value.finish();}
public:
    RequestRecording(){
        wchar_t *value=nullptr;size_t length=0;if(_wdupenv_s(&value,&length,L"NR_RENDER_CAPTURE_DIRECTORY"))throw std::runtime_error("cannot read recording directory");
        const std::unique_ptr<wchar_t,decltype(&std::free)> owner(value,&std::free);if(!value || !*value)return;directory=value;
        if(!directory.is_absolute() || !std::filesystem::is_directory(directory) || !std::filesystem::is_empty(directory) || std::filesystem::space(directory).available<maximum+uint64_t(1024)*1024*1024)throw std::runtime_error("recording requires an empty absolute directory and 17 GiB free");
        append("{\"recording_started\":true,\"maximum_request_bytes\":17179869184,\"contains_private_pixels\":true,\"performance_measurement_valid\":false}");
    }
    void request(uint32_t ticket,const std::vector<uint8_t> &data){
        if(directory.empty())return;if(data.size()>maximum-bytes)throw std::runtime_error("request recording budget exceeded");
        char filename[32];std::snprintf(filename,sizeof(filename),"request-%08u.bin",ticket);const auto path=directory/filename;
        indexed_stream::Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));DWORD written=0;
        if(!WriteFile(file.value,data.data(),DWORD(data.size()),&written,nullptr) || written!=data.size() || !FlushFileBuffers(file.value))throw std::runtime_error("request recording write failed");
        bytes+=data.size();append("{\"request_recorded\":true,\"ticket\":"+std::to_string(ticket)+",\"file\":\""+filename+"\",\"bytes\":"+std::to_string(data.size())+",\"sha256\":\""+digest(data.data(),data.size())+"\",\"cumulative_bytes\":"+std::to_string(bytes)+"}");
    }
    void response(uint32_t ticket,const std::vector<uint8_t> &data){
        if(directory.empty())return;if(data.size()<128)throw std::runtime_error("recorded response layout rejected");
        append("{\"provider_response_recorded\":true,\"ticket\":"+std::to_string(ticket)+",\"pixel_bytes\":"+std::to_string(data.size()-128)+",\"pixels_sha256\":\""+digest(data.data()+128,data.size()-128)+"\",\"delivery_acknowledged\":false}");
    }
};
}
