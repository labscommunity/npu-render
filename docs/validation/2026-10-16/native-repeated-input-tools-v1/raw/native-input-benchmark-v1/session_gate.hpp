#pragma once
#include <windows.h>
#include <wtsapi32.h>
#include <cwchar>
#include <vector>
#include <string>
namespace nr::present {
inline bool session_unlocked(DWORD session) {
    LPWSTR name=nullptr;DWORD bytes=0;
    if(!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,WTSUserName,&name,&bytes))return false;
    const bool has_user=name && bytes>=sizeof(wchar_t) && *name;WTSFreeMemory(name);if(!has_user)return false;
    LPWSTR raw=nullptr;if(!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,WTSSessionInfoEx,&raw,&bytes))return false;
    const auto info=reinterpret_cast<const WTSINFOEXW*>(raw);
    const bool ready=bytes>=sizeof(WTSINFOEXW) && info->Level==1 && info->Data.WTSInfoExLevel1.SessionId==session && info->Data.WTSInfoExLevel1.SessionState==WTSActive && info->Data.WTSInfoExLevel1.SessionFlags==WTS_SESSIONSTATE_UNLOCK;
    WTSFreeMemory(raw);return ready;
}
inline bool current_input_is_default() {
    const auto desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);if(!desktop)return false;
    wchar_t name[128]{};DWORD bytes=0;const bool ready=GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&bytes) && bytes<=sizeof(name) && !std::wcscmp(name,L"Default");CloseDesktop(desktop);return ready;
}

inline bool console_identity_matches(DWORD session){
    LPWSTR name=nullptr,domain=nullptr;DWORD bytes=0;
    if(!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,WTSUserName,&name,&bytes))return false;
    const std::wstring user=name?name:L"";WTSFreeMemory(name);if(user.empty())return false;
    if(!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,WTSDomainName,&domain,&bytes))return false;
    const std::wstring authority=domain?domain:L"";WTSFreeMemory(domain);if(authority.empty())return false;
    const auto account=authority+L"\\"+user;DWORD sid_bytes=0,domain_chars=0;SID_NAME_USE use{};
    LookupAccountNameW(nullptr,account.c_str(),nullptr,&sid_bytes,nullptr,&domain_chars,&use);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || !sid_bytes || sid_bytes>SECURITY_MAX_SID_SIZE || !domain_chars || domain_chars>32768)return false;
    std::vector<unsigned char> expected(sid_bytes);std::vector<wchar_t> resolved(domain_chars);
    if(!LookupAccountNameW(nullptr,account.c_str(),expected.data(),&sid_bytes,resolved.data(),&domain_chars,&use) || use!=SidTypeUser)return false;
    HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
    DWORD token_bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&token_bytes);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || !token_bytes || token_bytes>65536){CloseHandle(token);return false;}
    std::vector<unsigned char> data(token_bytes);const bool read=GetTokenInformation(token,TokenUser,data.data(),token_bytes,&token_bytes)!=FALSE;CloseHandle(token);
    return read && EqualSid(expected.data(),reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid)!=FALSE;
}
inline bool current_console_default(){DWORD session=0;return ProcessIdToSessionId(GetCurrentProcessId(),&session) && session && session==WTSGetActiveConsoleSessionId() && session_unlocked(session) && current_input_is_default();}
}
