#pragma once
#include "indexed_pipe.hpp"
#include <sddl.h>
#include <cwchar>
#include <cerrno>
namespace nr::registered_lab {
class NativeCompositorPeer {
    indexed_stream::Handle process;
    DWORD process_id=0,session=0;
    uint64_t created=0;
    std::wstring identity;
    static uint64_t creation(HANDLE h){FILETIME c{},e{},k{},u{};indexed_exchange::require(GetProcessTimes(h,&c,&e,&k,&u)!=FALSE);return (uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
public:
    static uint64_t number(const wchar_t *text,uint64_t maximum){indexed_exchange::require(text && *text>=L'0' && *text<=L'9');wchar_t *end=nullptr;errno=0;const auto n=std::wcstoull(text,&end,10);indexed_exchange::require(!errno && end!=text && !*end && n && n<=maximum);return n;}
    static std::wstring user(HANDLE h){HANDLE raw=nullptr;indexed_exchange::require(OpenProcessToken(h,TOKEN_QUERY,&raw)!=FALSE);indexed_stream::Handle token(raw);DWORD size=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&size);indexed_exchange::require(GetLastError()==ERROR_INSUFFICIENT_BUFFER && size && size<=65536);std::vector<uint8_t> bytes(size);indexed_exchange::require(GetTokenInformation(token.value,TokenUser,bytes.data(),size,&size)!=FALSE);LPWSTR s=nullptr;indexed_exchange::require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&s)!=FALSE);try{std::wstring result(s);LocalFree(s);return result;}catch(...){LocalFree(s);throw;}}
    NativeCompositorPeer(DWORD pid,uint64_t timestamp,unsigned kind=0):process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid)),process_id(pid),created(timestamp){
        indexed_exchange::require(kind<=2);
        indexed_exchange::require(pid && timestamp && ProcessIdToSessionId(pid,&session) && (kind?session==0:session!=0));
        wchar_t path[32768]{},system[MAX_PATH]{};DWORD length=32768;indexed_exchange::require(QueryFullProcessImageNameW(process.value,0,path,&length)!=FALSE && GetSystemDirectoryW(system,MAX_PATH)!=0);
        std::wstring expected;
        if(kind==1){wchar_t own[32768]{};const auto n=GetModuleFileNameW(nullptr,own,32768);indexed_exchange::require(n && n<32768);expected=own;const auto slash=expected.find_last_of(L"\\/");indexed_exchange::require(slash!=std::wstring::npos);expected.resize(slash+1);expected+=L"nr_native_service_probe.exe";}else expected=std::wstring(system)+(kind==2?LR"(\WUDFHost.exe)":LR"(\dwm.exe)");
        indexed_exchange::require(!_wcsicmp(path,expected.c_str()));
        identity=user(process.value);indexed_exchange::require(identity==(kind?std::wstring(L"S-1-5-19"):L"S-1-5-90-0-"+std::to_wstring(session)));verify();
    }
    void verify()const{DWORD current=0;indexed_exchange::require(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT && GetProcessId(process.value)==process_id && creation(process.value)==created && ProcessIdToSessionId(process_id,&current) && current==session && user(process.value)==identity);}
    const std::wstring &sid()const{return identity;}
    DWORD pid()const{return process_id;}
    DWORD session_id()const{return session;}
    uint64_t creation_time()const{return created;}
    void authorize(DWORD actual_client,const std::wstring &worker_user)const{
        if(actual_client==process_id){verify();return;}
        indexed_stream::Handle client(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,actual_client));
        const auto actor=user(client.value);indexed_exchange::require(actor==worker_user || actor==L"S-1-5-18");
        indexed_exchange::require(WaitForSingleObject(client.value,0)==WAIT_TIMEOUT);
    }
};
}
