#define NOMINMAX
#define NR_NATIVE_PROBE 1
#define NR_LAB_TRACE_ROOT LR"(C:\ProgramData\T8-NpuRender-TraceContractV1)"
#include "lab_trace.hpp"
#include <sddl.h>
#include <fstream>
#include <iterator>
namespace n = nr::native_probe;
namespace l = nr::registered_lab;
void check_at(bool value, int line) { if (!value) { std::fprintf(stderr,"trace assertion line %d (Win32 %lu)\n",line,GetLastError()); throw std::runtime_error("trace lifecycle contract failed"); } }
#define check(value) check_at((value), __LINE__)
struct Child {
    PROCESS_INFORMATION info{};
    Child() {
        wchar_t module[32768]{};
        check(GetModuleFileNameW(nullptr, module, DWORD(std::size(module))) != 0);
        std::wstring command = L"\"" + std::wstring(module) + L"\" --hold";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        check(CreateProcessW(module, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info) != 0);
    }
    void stop() { check(TerminateProcess(info.hProcess, 0) != 0); check(WaitForSingleObject(info.hProcess, 3000) == WAIT_OBJECT_0); }
    ~Child() {
        if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(info.hProcess, 1); WaitForSingleObject(info.hProcess, 3000); }
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
    }
    Child(const Child &) = delete;
};
std::string contents(const std::wstring &path) {
    std::ifstream f(path); check(bool(f));
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
int wmain(int argc, wchar_t **argv) try {
    if (argc == 2 && !std::wcscmp(argv[1], L"--hold")) { Sleep(25000); return 0; }
    check(argc == 1);
    const std::wstring root = NR_LAB_TRACE_ROOT, archived = root + L".archived";
    check(GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(archived.c_str()) == INVALID_FILE_ATTRIBUTES);
    check(CreateDirectoryW(root.c_str(), nullptr) != 0);
    check(l::lab_trace_session() == 0);
    Child controller, first_worker, second_worker;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    check(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;AU)", SDDL_REVISION_1, &descriptor, nullptr) != 0);
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const auto name = n::mapping_name(GetCurrentProcessId());
    const auto raw = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, sizeof(n::Lease), name.c_str());
    const auto error = GetLastError(); LocalFree(descriptor);
    n::Handle mapping(raw); check(error != ERROR_ALREADY_EXISTS);
    DWORD session = 0; check(ProcessIdToSessionId(GetCurrentProcessId(), &session) != 0);
    n::Lease lease; n::set_consumer_sid(lease, GetCurrentProcess());
    lease.issued_tick = GetTickCount64(); lease.deadline_tick = lease.issued_tick + 20000;
    lease.controller = {controller.info.dwProcessId, session, n::creation_time(controller.info.hProcess)};
    lease.worker = {first_worker.info.dwProcessId, session, n::creation_time(first_worker.info.hProcess)};
    lease.consumer = {GetCurrentProcessId(), session, n::creation_time(GetCurrentProcess())};
    lease.nonce[0] = 1; std::memset(lease.pipe_suffix, 'a', sizeof(lease.pipe_suffix));
    auto publish = [&] {
        const auto view = MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, sizeof(lease)); check(view != nullptr);
        std::memcpy(view, &lease, sizeof(lease)); check(UnmapViewOfFile(view) != 0);
    };
    publish(); check(l::lab_trace_session() == 1);
    l::lab_trace("{\"trial\":1}\n");
    const auto file = L"\\process-" + std::to_wstring(GetCurrentProcessId()) + L".jsonl";
    check(contents(root + file).find("\"trial\":1") != std::string::npos);
    first_worker.stop(); check(l::lab_trace_session() == 0);
    l::lab_trace("{\"invalid\":true}\n");
    check(contents(root + file).find("invalid") == std::string::npos);
    // Windows refuses a directory rename while a child file is open, even
    // with FILE_SHARE_DELETE. Archive the file itself, which is permitted.
    check(CreateDirectoryW(archived.c_str(), nullptr) != 0);
    check(MoveFileW((root + file).c_str(), (archived + file).c_str()) != 0);
    check(RemoveDirectoryW(root.c_str()) != 0);
    check(CreateDirectoryW(root.c_str(), nullptr) != 0);
    lease.worker = {second_worker.info.dwProcessId, session, n::creation_time(second_worker.info.hProcess)};
    lease.nonce[0] = 0; publish(); check(l::lab_trace_session() == 0);
    lease.nonce[0] = 2; publish(); check(l::lab_trace_session() == 2);
    l::lab_trace("{\"trial\":2}\n");
    const auto old_log = contents(archived + file), new_log = contents(root + file);
    check(old_log.find("\"trial\":1") != std::string::npos && old_log.find("\"trial\":2") == std::string::npos);
    check(new_log.find("\"trial\":2") != std::string::npos && new_log.find("\"trial\":1") == std::string::npos);
    check(new_log.find("\"event\":\"process\"") != std::string::npos);
    controller.stop(); check(l::lab_trace_session() == 0);
    l::lab_trace("{\"invalid\":true}\n"); check(contents(root + file) == new_log);
    std::printf("{\"trace_lifecycle_contract\":true,\"passed\":true,\"same_process_leases\":2,\"terminated_worker_rejected\":true,\"invalid_replacement_rejected\":true,\"terminated_controller_rejected\":true,\"archived_log_unchanged\":true,\"fresh_log_opened\":true,\"driver_installed\":false}\n");
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s (Win32 %lu)\n", e.what(), GetLastError()); return 1; }
