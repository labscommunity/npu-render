#include "lease.hpp"
#include <sddl.h>
#include <cstdio>
#include <cwchar>
#include <utility>
#include <iterator>
namespace n = nr::native_probe;
void check(bool value) { if (!value) throw std::runtime_error("native admission contract failed"); }
struct Descriptor {
    PSECURITY_DESCRIPTOR value = nullptr;
    explicit Descriptor(const wchar_t *sddl) { check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &value, nullptr) != 0); }
    ~Descriptor() { LocalFree(value); }
};
struct Child {
    PROCESS_INFORMATION info{};
    Child(const std::wstring &arguments, bool suspended) {
        wchar_t module[32768]{}; const DWORD length = GetModuleFileNameW(nullptr, module, DWORD(std::size(module)));
        check(length && length < std::size(module));
        std::wstring command = L"\"" + std::wstring(module) + L"\" " + arguments;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        check(CreateProcessW(module, command.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW | (suspended ? CREATE_SUSPENDED : 0), nullptr, nullptr, &startup, &info) != 0);
    }
    ~Child() {
        if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(info.hProcess, 1); WaitForSingleObject(info.hProcess, 3000); }
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
    }
    Child(const Child &) = delete;
};
int wmain(int argc, wchar_t **argv) try {
    if (argc == 2 && !std::wcscmp(argv[1], L"--hold")) { Sleep(10000); return 0; }
    if (argc == 4 && !std::wcscmp(argv[1], L"--consume")) {
        bool admitted = false;
        try {
            const auto record = n::read_mapping(argv[2]); n::Admission admission(record);
            admission.verify(); check(GetProcessId(admission.worker_handle()) == record.worker.pid);
            check(admission.pipe_name() == L"\\\\.\\pipe\\npu-render-indexed-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"); admitted = true;
        } catch (const std::exception &) {}
        return admitted == (!std::wcscmp(argv[3], L"accept")) ? 0 : 1;
    }
    check(argc == 1);
    unsigned shape_cases = 0, acl_cases = 0, process_cases = 0;
    n::Lease base; base.issued_tick = 9000; base.deadline_tick = 11000; base.scope = 0; n::set_consumer_sid(base,GetCurrentProcess());
    base.controller = {1,4,17}; base.worker = {2,4,18}; base.consumer = {3,4,19}; base.nonce[0] = 7;
    std::memset(base.pipe_suffix, 'a', sizeof(base.pipe_suffix));
    check(n::shape_valid(base,10000,3,19,4)); ++shape_cases;
    for (unsigned kind = 0; kind < 24; ++kind) {
        auto r = base;
        switch (kind) {
        case 0: r.magic = 0; break;
        case 1: ++r.version; break;
        case 2: --r.bytes; break;
        case 3: r.scope = 2; break;
        case 4: r.issued_tick = 0; break;
        case 5: r.issued_tick = 10001; break;
        case 6: r.deadline_tick = 10000; break;
        case 7: r.deadline_tick = r.issued_tick + 120001; break;
        case 8: r.controller.pid = 0; break;
        case 9: r.worker.pid = 0; break;
        case 10: r.consumer.pid = 0; break;
        case 11: ++r.consumer.created; break;
        case 12: ++r.consumer.session; break;
        case 13: r.controller.session = 5; break;
        case 14: r.worker.session = 5; break;
        case 15: r.consumer.session = 5; break;
        case 16: r.controller.created = 0; break;
        case 17: r.worker.created = 0; break;
        case 18: r.controller.pid = r.worker.pid; break;
        case 19: r.controller.pid = r.consumer.pid; break;
        case 20: r.worker.pid = r.consumer.pid; break;
        case 21: r.nonce[0] = 0; break;
        case 22: r.pipe_suffix[0] = 'G'; break;
        case 23: r.reserved[3] = 1; break;
        }
        check(!n::shape_valid(r,10000,3,19,4)); ++shape_cases;
    }
    for(unsigned kind=0;kind<6;++kind){auto value=base;switch(kind){case 0:value.sid_bytes=0;break;case 1:value.sid_bytes=69;break;case 2:value.consumer_sid[0]=0;break;case 3:value.consumer_sid[1]=16;break;case 4:value.consumer_sid[67]=1;break;case 5:value.reserved[4]=1;break;}check(!n::shape_valid(value,10000,3,19,4));++shape_cases;}
    auto desktop=base;desktop.scope=1;desktop.controller.session=desktop.worker.session=0;desktop.consumer.session=4;
    PSID dwm_sid=nullptr;check(ConvertStringSidToSidW(L"S-1-5-90-0-4",&dwm_sid)!=FALSE);desktop.sid_bytes=GetLengthSid(dwm_sid);std::memset(desktop.consumer_sid,0,sizeof(desktop.consumer_sid));check(CopySid(sizeof(desktop.consumer_sid),desktop.consumer_sid,dwm_sid)!=FALSE);LocalFree(dwm_sid);
    check(n::shape_valid(desktop,10000,3,19,4));++shape_cases;desktop.deadline_tick=desktop.issued_tick+900000;check(n::shape_valid(desktop,10000,3,19,4));++shape_cases;++desktop.deadline_tick;check(!n::shape_valid(desktop,10000,3,19,4));++shape_cases;desktop.deadline_tick=11000;desktop.scope=0;check(!n::shape_valid(desktop,10000,3,19,4));++shape_cases;
    const auto console=WTSGetActiveConsoleSessionId();std::printf("{\"console_session\":%lu,\"interactive_session_unlocked\":%s,\"compositor_execution_tested\":false}\n",console,n::session_unlocked(console)?"true":"false");
    const std::pair<const wchar_t *, bool> descriptors[]{
        {L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;AU)",true},
        {L"O:SYG:SYD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;AU)",true},
        {L"O:AUG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;AU)",false},
        {L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020006;;;AU)",false},
        {L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;WD;;;AU)",false},
        {L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;WO;;;AU)",false},
        {L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;AU)",false},
        {L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GW;;;AU)",false},
        {L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020014;;;AU)",false}
    };
    for (const auto &entry : descriptors) { Descriptor descriptor(entry.first); check(n::readonly_to_unprivileged(descriptor.value) == entry.second); ++acl_cases; }
    Descriptor null_acl(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)");
    // Build an absolute descriptor explicitly to exercise absent and null DACLs.
    SECURITY_DESCRIPTOR absolute{}; check(InitializeSecurityDescriptor(&absolute, SECURITY_DESCRIPTOR_REVISION) != 0);
    PSID owner = nullptr; BOOL ignored = FALSE; check(GetSecurityDescriptorOwner(null_acl.value,&owner,&ignored) != 0);
    check(SetSecurityDescriptorOwner(&absolute,owner,FALSE) != 0);
    check(!n::readonly_to_unprivileged(&absolute)); ++acl_cases;
    check(SetSecurityDescriptorDacl(&absolute,TRUE,nullptr,FALSE) != 0);
    check(!n::readonly_to_unprivileged(&absolute)); ++acl_cases;
    DWORD session = 0; check(ProcessIdToSessionId(GetCurrentProcessId(), &session) != 0);
    Child worker(L"--hold",false);
    for (unsigned kind = 0; kind < 5; ++kind) {
        wchar_t name[160]{}; swprintf_s(name,L"Global\\T8.NpuRender.NativeSession.Test.%lu.%llu.%u",GetCurrentProcessId(),static_cast<unsigned long long>(GetTickCount64()),kind);
        Descriptor descriptor(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;AU)");
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor.value,FALSE};
        HANDLE raw = CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(n::Lease),name);
        const DWORD error = GetLastError(); n::Handle mapping(raw); check(error != ERROR_ALREADY_EXISTS);
        Child consumer(L"--consume \""+std::wstring(name)+L"\" "+(kind==0?L"accept":L"reject"),true);
        n::Lease record; n::set_consumer_sid(record,consumer.info.hProcess);record.issued_tick=GetTickCount64();record.deadline_tick=record.issued_tick+10000;
        record.controller={GetCurrentProcessId(),session,n::creation_time(GetCurrentProcess())};
        record.worker={worker.info.dwProcessId,session,n::creation_time(worker.info.hProcess)};
        record.consumer={consumer.info.dwProcessId,session,n::creation_time(consumer.info.hProcess)};
        record.nonce[0]=1;std::memset(record.pipe_suffix,'a',sizeof(record.pipe_suffix));
        if(kind==1)++record.worker.created;
        if(kind==2)++record.controller.created;
        if(kind==3){record.issued_tick-=100;record.deadline_tick=record.issued_tick+1;}
        if(kind==4)record.consumer.pid=GetCurrentProcessId();
        auto view=MapViewOfFile(mapping.value,FILE_MAP_WRITE,0,0,sizeof(record));check(view!=nullptr);
        std::memcpy(view,&record,sizeof(record));check(UnmapViewOfFile(view)!=0);
        check(ResumeThread(consumer.info.hThread)!=DWORD(-1));check(WaitForSingleObject(consumer.info.hProcess,10000)==WAIT_OBJECT_0);
        DWORD code=1;check(GetExitCodeProcess(consumer.info.hProcess,&code)!=0);
        std::printf("{\"process_case\":%u,\"child_exit_code\":%lu}\n",kind,code);check(code==0);++process_cases;
    }
    std::printf("{\"native_session_admission_contract\":true,\"passed\":true,\"shape_cases\":%u,\"acl_cases\":%u,\"separate_process_cases\":%u,\"driver_installed\":false,\"npu_execution_tested\":false}\n",shape_cases,acl_cases,process_cases);
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr,"%s (last Win32 error %lu)\n",e.what(),GetLastError());return 1; }
