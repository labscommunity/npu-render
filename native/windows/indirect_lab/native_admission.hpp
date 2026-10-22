#pragma once
#include "../registered_lab/native_probe_lease.hpp"
#include <memory>
namespace nr::indirect {
inline bool current_host_identity(){
    DWORD session=UINT32_MAX;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session) || session || !nr::native_probe::current_image_is(L"WUDFHost.exe"))return false;
    try{const auto sid=nr::native_probe::user_sid(GetCurrentProcess());return IsWellKnownSid(const_cast<uint8_t*>(sid.data()),WinLocalServiceSid)!=FALSE;}catch(...){return false;}
}
inline std::unique_ptr<nr::native_probe::Admission> await_native_admission(DWORD milliseconds=10000){
    nr::native_probe::require(milliseconds && milliseconds<=10000 && current_host_identity());
    const auto deadline=GetTickCount64()+milliseconds;const auto name=nr::native_probe::mapping_name(GetCurrentProcessId())+L".Ready";
    while(GetTickCount64()<deadline){
        HANDLE ready=OpenEventW(SYNCHRONIZE,FALSE,name.c_str());
        if(ready){const auto now=GetTickCount64();const auto wait=now<deadline?WaitForSingleObject(ready,DWORD(deadline-now)):WAIT_TIMEOUT;CloseHandle(ready);nr::native_probe::require(wait==WAIT_OBJECT_0);const auto record=nr::native_probe::read_mapping();nr::native_probe::require(record.scope==2);return std::make_unique<nr::native_probe::Admission>(record);}
        nr::native_probe::require(GetLastError()==ERROR_FILE_NOT_FOUND);Sleep(20);
    }
    throw std::runtime_error("Native indirect host admission deadline expired");
}
}
