#define NOMINMAX
#include <windows.h>
#include <authz.h>
#include <sddl.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <stdexcept>
struct Handle{HANDLE value;explicit Handle(HANDLE h):value(h){}~Handle(){if(value)CloseHandle(value);}Handle(const Handle&)=delete;};
void check(bool v){if(!v)throw std::runtime_error("read-only access inspection failed");}
std::wstring user_sid(HANDLE token){DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);check(GetLastError()==ERROR_INSUFFICIENT_BUFFER && bytes && bytes<65536);std::vector<unsigned char> data(bytes);check(GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes)!=FALSE);LPWSTR text=nullptr;check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&text)!=FALSE);std::wstring result(text);LocalFree(text);return result;}
struct Access{DWORD granted=0,error=0;bool allowed=false;};
Access evaluate(HANDLE token,const std::wstring &sddl){
    PSECURITY_DESCRIPTOR sd=nullptr;AUTHZ_RESOURCE_MANAGER_HANDLE manager=nullptr;AUTHZ_CLIENT_CONTEXT_HANDLE context=nullptr;
    try{check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr)!=FALSE);check(AuthzInitializeResourceManager(AUTHZ_RM_FLAG_NO_AUDIT,nullptr,nullptr,nullptr,L"NPU Render read-only DACL inspection",&manager)!=FALSE);LUID id{};check(AuthzInitializeContextFromToken(0,token,manager,nullptr,id,nullptr,&context)!=FALSE);
        AUTHZ_ACCESS_REQUEST request{};request.DesiredAccess=FILE_GENERIC_READ|FILE_GENERIC_WRITE;Access result;DWORD sacl=0;AUTHZ_ACCESS_REPLY reply{};reply.ResultListLength=1;reply.GrantedAccessMask=&result.granted;reply.Error=&result.error;reply.SaclEvaluationResults=&sacl;check(AuthzAccessCheck(0,context,&request,nullptr,sd,nullptr,0,&reply,nullptr)!=FALSE);result.allowed=!result.error && (result.granted&request.DesiredAccess)==request.DesiredAccess;AuthzFreeContext(context);AuthzFreeResourceManager(manager);LocalFree(sd);return result;
    }catch(...){if(context)AuthzFreeContext(context);if(manager)AuthzFreeResourceManager(manager);if(sd)LocalFree(sd);throw;}
}
int wmain(int argc,wchar_t **argv)try{
    check(argc==2);wchar_t *end=nullptr;const auto pid=std::wcstoul(argv[1],&end,10);check(end!=argv[1] && !*end && pid);
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));check(process.value!=nullptr);
    WCHAR path[32768]{},system[MAX_PATH]{};DWORD length=32768;check(QueryFullProcessImageNameW(process.value,0,path,&length)!=FALSE);check(GetSystemDirectoryW(system,MAX_PATH)!=0);check(!_wcsicmp(path,(std::wstring(system)+L"\\dwm.exe").c_str()));DWORD session=0;check(ProcessIdToSessionId(pid,&session)!=FALSE);
    HANDLE own_raw=nullptr;check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&own_raw)!=FALSE);Handle own(own_raw);const auto caller=user_sid(own.value);
    // File-specific masks model GENERIC_ALL/READ/WRITE after named-pipe mapping.
    const auto current=L"O:SYG:SYD:P(A;;0x001f01ff;;;SY)(A;;0x001f01ff;;;"+caller+L")";
    const auto control=evaluate(own.value,current);check(control.allowed);
    HANDLE raw=nullptr;if(!OpenProcessToken(process.value,TOKEN_QUERY,&raw)){
        const auto error=GetLastError();std::printf("{\"inspection_completed\":true,\"dwm_pid\":%lu,\"dwm_session\":%lu,\"current_user_control_allowed\":true,\"dwm_token_query_available\":false,\"token_query_error\":%lu,\"security_changes\":false,\"actual_pipe_connection_tested\":false}\n",pid,session,error);return 0;
    }
    Handle token(raw);const auto dwm=user_sid(token.value);check(dwm==L"S-1-5-90-0-"+std::to_wstring(session));const auto existing=evaluate(token.value,current);const auto candidate=evaluate(token.value,current+L"(A;;0x001f01ff;;;"+dwm+L")");check(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT);
    std::printf("{\"inspection_completed\":true,\"dwm_pid\":%lu,\"dwm_session\":%lu,\"current_user_control_allowed\":true,\"dwm_token_query_available\":true,\"dwm_current_worker_dacl_allowed\":%s,\"dwm_explicit_identity_dacl_allowed\":%s,\"current_dacl_error\":%lu,\"candidate_dacl_error\":%lu,\"security_changes\":false,\"actual_pipe_connection_tested\":false,\"mandatory_integrity_tested\":false}\n",pid,session,existing.allowed?"true":"false",candidate.allowed?"true":"false",existing.error,candidate.error);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s: %lu\n",e.what(),GetLastError());return 1;}
