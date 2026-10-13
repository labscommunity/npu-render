#define NOMINMAX
#include <windows.h>
#include <authz.h>
#include <sddl.h>
#include <aclapi.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <stdexcept>
struct Handle{HANDLE value;explicit Handle(HANDLE h):value(h){}~Handle(){if(value)CloseHandle(value);}Handle(const Handle&)=delete;};
void check(bool v){if(!v)throw std::runtime_error("read-only access inspection failed");}
std::wstring user_sid(HANDLE token){DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);check(GetLastError()==ERROR_INSUFFICIENT_BUFFER && bytes && bytes<65536);std::vector<unsigned char> data(bytes);check(GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes)!=FALSE);LPWSTR text=nullptr;check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&text)!=FALSE);std::wstring result(text);LocalFree(text);return result;}
struct Access{DWORD granted=0,error=0;bool allowed=false;};
Access evaluate(HANDLE token,PSECURITY_DESCRIPTOR sd,DWORD desired){
    AUTHZ_RESOURCE_MANAGER_HANDLE manager=nullptr;AUTHZ_CLIENT_CONTEXT_HANDLE context=nullptr;
    try{check(AuthzInitializeResourceManager(AUTHZ_RM_FLAG_NO_AUDIT,nullptr,nullptr,nullptr,L"NPU Render read-only DACL inspection",&manager)!=FALSE);LUID id{};check(AuthzInitializeContextFromToken(0,token,manager,nullptr,id,nullptr,&context)!=FALSE);
        AUTHZ_ACCESS_REQUEST request{};request.DesiredAccess=desired;Access result;DWORD sacl=0;AUTHZ_ACCESS_REPLY reply{};reply.ResultListLength=1;reply.GrantedAccessMask=&result.granted;reply.Error=&result.error;reply.SaclEvaluationResults=&sacl;check(AuthzAccessCheck(0,context,&request,nullptr,sd,nullptr,0,&reply,nullptr)!=FALSE);result.allowed=!result.error && (result.granted&request.DesiredAccess)==request.DesiredAccess;AuthzFreeContext(context);AuthzFreeResourceManager(manager);return result;
    }catch(...){if(context)AuthzFreeContext(context);if(manager)AuthzFreeResourceManager(manager);throw;}
}

Access text_access(HANDLE token,const std::wstring &sddl,DWORD desired){PSECURITY_DESCRIPTOR sd=nullptr;check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),1,&sd,nullptr)!=FALSE);try{const auto result=evaluate(token,sd,desired);LocalFree(sd);return result;}catch(...){LocalFree(sd);throw;}}
DWORD restricted_count(HANDLE token){DWORD bytes=0;GetTokenInformation(token,TokenRestrictedSids,nullptr,0,&bytes);check(bytes && bytes<65536);std::vector<unsigned char> data(bytes);check(GetTokenInformation(token,TokenRestrictedSids,data.data(),bytes,&bytes)!=FALSE);return reinterpret_cast<TOKEN_GROUPS*>(data.data())->GroupCount;}
void peer(DWORD pid,bool compositor,HANDLE own_token,PSECURITY_DESCRIPTOR process_sd){
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));check(process.value!=nullptr);
    WCHAR path[32768]{},system[MAX_PATH]{};DWORD length=32768;check(QueryFullProcessImageNameW(process.value,0,path,&length)!=FALSE && GetSystemDirectoryW(system,MAX_PATH));
    check(!_wcsicmp(path,(std::wstring(system)+(compositor?L"\dwm.exe":L"\WUDFHost.exe")).c_str()));DWORD session=0;check(ProcessIdToSessionId(pid,&session));
    HANDLE raw=nullptr;check(OpenProcessToken(process.value,TOKEN_QUERY,&raw));Handle token(raw);const auto sid=user_sid(token.value);check(sid==(compositor?L"S-1-5-90-0-"+std::to_wstring(session):L"S-1-5-19"));
    const auto caller=user_sid(own_token);const auto base=L"O:SYG:SYD:P(A;;0x001f01ff;;;SY)(A;;0x001f01ff;;;"+caller+L")";
    const auto pipe=text_access(token.value,base,FILE_GENERIC_READ|FILE_GENERIC_WRITE);
    const auto pipe_added=text_access(token.value,base+L"(A;;0x0012019f;;;"+sid+L")",FILE_GENERIC_READ|FILE_GENERIC_WRITE);
    const auto mapping=text_access(token.value,L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;AU)",FILE_MAP_READ|READ_CONTROL);
    const auto mapping_added=text_access(token.value,L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;"+sid+L")",FILE_MAP_READ|READ_CONTROL);
    constexpr DWORD process_rights=PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE;
    const auto current=evaluate(token.value,process_sd,process_rights);
    BOOL present=FALSE,defaulted=FALSE;PACL acl=nullptr;PSID owner=nullptr;check(GetSecurityDescriptorDacl(process_sd,&present,&acl,&defaulted) && present && acl);check(GetSecurityDescriptorOwner(process_sd,&owner,&defaulted));
    EXPLICIT_ACCESSW grant{};grant.grfAccessPermissions=process_rights;grant.grfAccessMode=GRANT_ACCESS;grant.grfInheritance=NO_INHERITANCE;grant.Trustee.TrusteeForm=TRUSTEE_IS_SID;grant.Trustee.TrusteeType=TRUSTEE_IS_USER;
    PSID parsed=nullptr;check(ConvertStringSidToSidW(sid.c_str(),&parsed));grant.Trustee.ptstrName=reinterpret_cast<LPWSTR>(parsed);PACL candidate=nullptr;const auto status=SetEntriesInAclW(1,&grant,acl,&candidate);LocalFree(parsed);check(status==ERROR_SUCCESS);
    SECURITY_DESCRIPTOR proposed{};check(InitializeSecurityDescriptor(&proposed,SECURITY_DESCRIPTOR_REVISION) && SetSecurityDescriptorOwner(&proposed,owner,FALSE) && SetSecurityDescriptorDacl(&proposed,TRUE,candidate,FALSE));const auto added=evaluate(token.value,&proposed,process_rights);LocalFree(candidate);
    check(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT);
    std::printf("{\"peer\":\"%s\",\"pid\":%lu,\"session\":%lu,\"restricted_sid_count\":%lu,\"pipe_current_allowed\":%s,\"pipe_scoped_sid_allowed\":%s,\"mapping_authenticated_users_allowed\":%s,\"mapping_scoped_sid_allowed\":%s,\"owned_probe_process_current_allowed\":%s,\"owned_probe_process_scoped_sid_allowed\":%s,\"process_requested_mask\":%lu,\"process_error\":%lu,\"process_candidate_error\":%lu,\"actual_peer_execution\":false,\"mandatory_integrity_tested\":false,\"security_changes\":false}\n",compositor?"dwm":"umdf",pid,session,restricted_count(token.value),pipe.allowed?"true":"false",pipe_added.allowed?"true":"false",mapping.allowed?"true":"false",mapping_added.allowed?"true":"false",current.allowed?"true":"false",added.allowed?"true":"false",process_rights,current.error,added.error);
}
int wmain(int argc,wchar_t **argv)try{
    check(argc==3);HANDLE raw=nullptr;check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw));Handle token(raw);
    PSECURITY_DESCRIPTOR sd=nullptr;check(GetSecurityInfo(GetCurrentProcess(),SE_KERNEL_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr,&sd)==ERROR_SUCCESS);
    try{for(int i=1;i<argc;++i){wchar_t *end=nullptr;const auto pid=std::wcstoul(argv[i],&end,10);check(pid && end!=argv[i] && !*end);peer(pid,i==1,token.value,sd);}LocalFree(sd);}catch(...){LocalFree(sd);throw;}
    std::printf("{\"inspection_completed\":true,\"peer_count\":2,\"changes_performed\":false}\n");return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s: %lu\n",e.what(),GetLastError());return 1;}
