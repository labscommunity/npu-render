#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace nr::native_probe {
constexpr wchar_t mapping_name[] = L"Local\\T8.NpuRender.NativeProbe.v1";
constexpr uint64_t signature = 0x31564250524e3854ull;
struct ProcessIdentity { uint32_t pid = 0, reserved = 0; uint64_t created = 0; };
struct Lease {
    uint64_t magic = signature;
    uint32_t version = 1, bytes = sizeof(Lease);
    uint64_t issued_tick = 0, deadline_tick = 0;
    ProcessIdentity controller, worker, consumer;
    uint32_t session = 0, flags = 0;
    uint64_t nonce[2]{};
    char pipe_suffix[32]{};
    uint32_t reserved[4]{};
};
static_assert(sizeof(Lease) == 152);
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
inline bool shape_valid(const Lease &r, uint64_t now, DWORD pid, uint64_t created, DWORD session) {
    if (r.magic != signature || r.version != 1 || r.bytes != sizeof(Lease) || r.flags ||
        !r.issued_tick || r.issued_tick > now || r.deadline_tick <= now ||
        r.deadline_tick <= r.issued_tick || r.deadline_tick - r.issued_tick > 120000 ||
        r.consumer.pid != pid || r.consumer.created != created || r.session != session ||
        !(r.nonce[0] || r.nonce[1])) return false;
    for (const auto &identity : {r.controller, r.worker, r.consumer})
        if (!identity.pid || !identity.created || identity.reserved) return false;
    if (r.controller.pid == r.worker.pid || r.controller.pid == r.consumer.pid || r.worker.pid == r.consumer.pid) return false;
    for (const auto ch : r.pipe_suffix) if (!(ch >= '0' && ch <= '9') && !(ch >= 'a' && ch <= 'f')) return false;
    for (const auto v : r.reserved) if (v) return false;
    return true;
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
inline Lease read_mapping(const wchar_t *name = mapping_name) {
    Handle mapping(OpenFileMappingW(FILE_MAP_READ | READ_CONTROL, FALSE, name));
    PSECURITY_DESCRIPTOR descriptor = nullptr; PSID owner = nullptr; PACL acl = nullptr;
    require(GetSecurityInfo(mapping.value, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                            &owner, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS);
    const bool permitted = readonly_to_unprivileged(descriptor); LocalFree(descriptor); require(permitted);
    const auto view = MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, sizeof(Lease)); require(view != nullptr);
    Lease record{}; std::memcpy(&record, view, sizeof(record)); UnmapViewOfFile(view); return record;
}
class Admission {
    Lease lease;
    Handle controller, worker;
    static HANDLE open(const ProcessIdentity &identity) {
        return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, identity.pid);
    }
    static void verify_process(HANDLE handle, const ProcessIdentity &identity, DWORD session) {
        DWORD actual_session = 0;
        require(GetProcessId(handle) == identity.pid && creation_time(handle) == identity.created &&
                WaitForSingleObject(handle, 0) == WAIT_TIMEOUT && ProcessIdToSessionId(identity.pid, &actual_session) && actual_session == session);
    }
public:
    explicit Admission(const Lease &record) : lease(record), controller(open(record.controller)), worker(open(record.worker)) { verify(); }
    void verify() const {
        DWORD session = 0; require(ProcessIdToSessionId(GetCurrentProcessId(), &session) != 0);
        require(shape_valid(lease, GetTickCount64(), GetCurrentProcessId(), creation_time(GetCurrentProcess()), session));
        verify_process(controller.value, lease.controller, session); verify_process(worker.value, lease.worker, session);
    }
    HANDLE worker_handle() const { verify(); return worker.value; }
    std::wstring pipe_name() const {
        verify(); const std::string suffix(lease.pipe_suffix, sizeof(lease.pipe_suffix));
        return L"\\\\.\\pipe\\npu-render-indexed-" + std::wstring(suffix.begin(), suffix.end());
    }
};
}
