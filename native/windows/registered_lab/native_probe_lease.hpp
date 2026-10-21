#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <wtsapi32.h>
#include <cwchar>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace nr::native_probe {
inline std::wstring mapping_name(DWORD pid) { return L"Global\\T8.NpuRender.NativeSession.v3."+std::to_wstring(pid); }
constexpr uint64_t signature = 0x33564250524e3854ull;
struct ProcessIdentity { uint32_t pid = 0, session = 0; uint64_t created = 0; };
struct Lease {
    uint64_t magic = signature;
    uint32_t version = 3, bytes = sizeof(Lease);
    uint64_t issued_tick = 0, deadline_tick = 0;
    ProcessIdentity controller, worker, consumer;
    uint32_t scope = 0, sid_bytes = 0; // 0 same-session; 1 compositor; 2 indirect host; 3 same-user interactive client.
    uint64_t nonce[2]{};
    char pipe_suffix[32]{};
    uint8_t consumer_sid[SECURITY_MAX_SID_SIZE]{};
    uint32_t desktop_session = 0, reserved[4]{};
};
static_assert(sizeof(Lease) == 224);
inline void require(bool ok) { if (!ok) throw std::runtime_error("native probe lease rejected"); }
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) { require(h && h != INVALID_HANDLE_VALUE); }
    ~Handle() { CloseHandle(value); }
    Handle(const Handle &) = delete;
};
inline uint64_t creation_time(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetProcessTimes(process, &created, &exited, &kernel, &user) != 0);
    return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
inline bool sid_valid(const Lease &r) {
    if(r.sid_bytes<8 || r.sid_bytes>sizeof(r.consumer_sid) || r.consumer_sid[0]!=SID_REVISION || r.consumer_sid[1]>SID_MAX_SUB_AUTHORITIES || r.sid_bytes!=8u+4u*r.consumer_sid[1])return false;
    if(!IsValidSid(const_cast<uint8_t*>(r.consumer_sid)))return false;
    for(size_t i=r.sid_bytes;i<sizeof(r.consumer_sid);++i)if(r.consumer_sid[i])return false;
    return true;
}
inline bool compositor_sid(const Lease &r) {
    if(!sid_valid(r) || r.consumer_sid[1]!=3)return false;
    auto sid=const_cast<uint8_t*>(r.consumer_sid);const SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;
    return !std::memcmp(GetSidIdentifierAuthority(sid),&nt,sizeof(nt)) && *GetSidSubAuthority(sid,0)==90 && *GetSidSubAuthority(sid,1)==0 && *GetSidSubAuthority(sid,2)==r.consumer.session;
}
inline bool shape_valid(const Lease &r, uint64_t now, DWORD pid, uint64_t created, DWORD session) {
    const auto maximum=(r.scope==0||r.scope==3)?120000u:2400000u;
    if(r.magic!=signature || r.version!=3 || r.bytes!=sizeof(Lease) || r.scope>3 || !sid_valid(r) ||
       !r.issued_tick || r.issued_tick>now || r.deadline_tick<=now || r.deadline_tick<=r.issued_tick || r.deadline_tick-r.issued_tick>maximum ||
       r.consumer.pid!=pid || r.consumer.created!=created || r.consumer.session!=session || !(r.nonce[0] || r.nonce[1]))return false;
    for(const auto &p:{r.controller,r.worker,r.consumer})if(!p.pid || !p.created || p.session==UINT32_MAX)return false;
    if(r.scope==0){if(r.desktop_session || r.controller.session!=session || r.worker.session!=session || compositor_sid(r))return false;}
    else {
        if(r.controller.session || r.worker.session || !r.desktop_session || r.desktop_session==UINT32_MAX)return false;
        if(r.scope==1 && (!session || r.desktop_session!=session || !compositor_sid(r)))return false;
        if(r.scope==2 && (session || !IsWellKnownSid(const_cast<uint8_t*>(r.consumer_sid),WinLocalServiceSid)))return false;
        if(r.scope==3 && (!session || r.desktop_session!=session || compositor_sid(r) || IsWellKnownSid(const_cast<uint8_t*>(r.consumer_sid),WinLocalSystemSid) || IsWellKnownSid(const_cast<uint8_t*>(r.consumer_sid),WinLocalServiceSid) || IsWellKnownSid(const_cast<uint8_t*>(r.consumer_sid),WinNetworkServiceSid)))return false;
    }
    if(r.controller.pid==r.worker.pid || r.controller.pid==pid || r.worker.pid==pid)return false;
    for(const auto c:r.pipe_suffix)if(!(c>='0' && c<='9') && !(c>='a' && c<='f'))return false;
    for(const auto v:r.reserved)if(v)return false;
    return true;
}
inline std::vector<uint8_t> user_sid(HANDLE process) {
    HANDLE raw=nullptr;require(OpenProcessToken(process,TOKEN_QUERY,&raw)!=FALSE);Handle token(raw);DWORD size=0;
    GetTokenInformation(token.value,TokenUser,nullptr,0,&size);require(GetLastError()==ERROR_INSUFFICIENT_BUFFER && size && size<=65536);
    std::vector<uint8_t> data(size);require(GetTokenInformation(token.value,TokenUser,data.data(),size,&size)!=FALSE);
    auto sid=reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;require(IsValidSid(sid)!=FALSE);const auto bytes=GetLengthSid(sid);require(bytes<=SECURITY_MAX_SID_SIZE);
    std::vector<uint8_t> result(bytes);require(CopySid(bytes,result.data(),sid)!=FALSE);return result;
}
inline void set_consumer_sid(Lease &r,HANDLE process) {
    const auto sid=user_sid(process);r.sid_bytes=DWORD(sid.size());std::memset(r.consumer_sid,0,sizeof(r.consumer_sid));std::memcpy(r.consumer_sid,sid.data(),sid.size());
}
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
inline bool current_image_is(const wchar_t *name) {
    wchar_t path[32768]{},root[MAX_PATH]{};DWORD bytes=32768;
    return QueryFullProcessImageNameW(GetCurrentProcess(),0,path,&bytes) && GetSystemDirectoryW(root,MAX_PATH) && !_wcsicmp(path,(std::wstring(root)+L"\\"+name).c_str());
}
inline bool administrator_or_system(PSID sid) {
    return sid && (IsWellKnownSid(sid, WinBuiltinAdministratorsSid) || IsWellKnownSid(sid, WinLocalSystemSid));
}
inline bool readonly_to_unprivileged(PSECURITY_DESCRIPTOR descriptor) {
    PSID owner = nullptr; BOOL defaulted = FALSE, present = FALSE; PACL acl = nullptr;
    if (!GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) || !administrator_or_system(owner) ||
        !GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) || !present || !acl || !IsValidAcl(acl)) return false;
    constexpr ACCESS_MASK write_bits = FILE_MAP_WRITE | 0x10u /* SECTION_EXTEND_SIZE */ | WRITE_DAC | WRITE_OWNER | DELETE | GENERIC_WRITE | GENERIC_ALL;
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void *raw = nullptr; if (!GetAce(acl, index, &raw)) return false;
        const auto header = static_cast<ACE_HEADER *>(raw);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) return false;
        const auto ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);
        if ((ace->Mask & write_bits) && !administrator_or_system(reinterpret_cast<PSID>(&ace->SidStart))) return false;
    }
    return true;
}
inline Lease read_mapping(const wchar_t *name = nullptr) {
    const auto own=mapping_name(GetCurrentProcessId());if(!name)name=own.c_str();
    Handle mapping(OpenFileMappingW(FILE_MAP_READ | READ_CONTROL, FALSE, name));
    PSECURITY_DESCRIPTOR descriptor = nullptr; PSID owner = nullptr; PACL acl = nullptr;
    require(GetSecurityInfo(mapping.value, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                            &owner, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS);
    const bool permitted = readonly_to_unprivileged(descriptor); LocalFree(descriptor); require(permitted);
    const auto view = MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, sizeof(Lease)); require(view != nullptr);
    Lease record{}; std::memcpy(&record, view, sizeof(record)); UnmapViewOfFile(view); return record;
}
// A lease may be renewed in place by its controller: issued_tick and deadline_tick advance
// (each grant still within the scope's maximum), everything else stays. A consumer re-reads
// its mapping as the cached deadline approaches and accepts only such a renewal.
constexpr uint64_t renewal_margin_ms = 5u * 60u * 1000u;
inline bool renewal_of(const Lease &current, const Lease &next) {
    Lease a = current, b = next; a.issued_tick = b.issued_tick = 0; a.deadline_tick = b.deadline_tick = 0;
    return next.issued_tick >= current.issued_tick && next.deadline_tick > current.deadline_tick && !std::memcmp(&a, &b, sizeof(Lease));
}
class Admission {
    Lease lease;
    Handle controller, worker;
    uint64_t last_refresh_tick = 0;
    // Re-read the mapping when the cached deadline is within the renewal margin; a renewal
    // that fails to read or validate leaves the cached lease in force until it expires.
    void refresh(uint64_t now, DWORD pid, uint64_t created, DWORD session) {
        if (lease.deadline_tick > now + renewal_margin_ms || now - last_refresh_tick < 15000) return;
        last_refresh_tick = now;
        try {
            const auto next = read_mapping();
            if (renewal_of(lease, next) && shape_valid(next, now, pid, created, session)) lease = next;
        } catch (const std::exception &) {}
    }
    static HANDLE open(const ProcessIdentity &identity) {
        return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, identity.pid);
    }
    static void verify_process(HANDLE handle, const ProcessIdentity &identity) {
        DWORD actual_session = 0;
        require(GetProcessId(handle) == identity.pid && creation_time(handle) == identity.created &&
                WaitForSingleObject(handle, 0) == WAIT_TIMEOUT && ProcessIdToSessionId(identity.pid, &actual_session) && actual_session == identity.session);
    }
public:
    explicit Admission(const Lease &record) : lease(record), controller(open(record.controller)), worker(open(record.worker)) { verify(); }
    // worker_required: the exchange needs a live worker to connect to; device creation does
    // not. A consumer whose worker has gone keeps its lease (the controller still owns the
    // admission) and keeps creating devices, running on its CPU fallback until the admission
    // ends: the loss of the worker must never make DWM's device creation fail (trial 18).
    void verify(bool worker_required = true) {
        DWORD session = 0; require(ProcessIdToSessionId(GetCurrentProcessId(), &session) != 0);
        const auto now = GetTickCount64(), created = creation_time(GetCurrentProcess()); const auto pid = GetCurrentProcessId();
        refresh(now, pid, created, session);
        require(shape_valid(lease, now, pid, created, session));
        const auto sid=user_sid(GetCurrentProcess());
        require(sid.size()==lease.sid_bytes && EqualSid(const_cast<uint8_t*>(lease.consumer_sid),const_cast<uint8_t*>(sid.data())));
        verify_process(controller.value, lease.controller);
        if (worker_required) verify_process(worker.value, lease.worker);
        if(lease.scope)require(session_unlocked(lease.desktop_session));
        // The compositor composes every desktop: the screen saver's, the lock screen's and UAC's
        // secure desktop switch the input desktop away from "Default", and DWM creates a new
        // graphics device for each. Requiring the Default desktop here made that creation fail
        // and DWM fail fast (0x889800b0, trials 14 and 16). Identity, session and SID still bind
        // the compositor; only interactive clients (scope 3) must sit on the Default desktop.
        if(lease.scope==1)require(current_image_is(L"dwm.exe"));
        if(lease.scope==2)require(current_image_is(L"WUDFHost.exe"));
        if(lease.scope==3){require(current_input_is_default());require(user_sid(worker.value)==sid&&user_sid(controller.value)==sid);}
    }
    HANDLE worker_handle() { verify(); return worker.value; }
    uint64_t deadline_tick() const { return lease.deadline_tick; }
    std::wstring pipe_name() {
        verify(); const std::string suffix(lease.pipe_suffix, sizeof(lease.pipe_suffix));
        return L"\\\\.\\pipe\\npu-render-indexed-" + std::wstring(suffix.begin(), suffix.end());
    }
};
}
