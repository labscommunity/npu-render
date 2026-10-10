// Finite interactive-desktop activation, with automatic restoration of prior paths.
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#ifdef NR_TOPOLOGY_GUI
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#endif
#include <vector>
#include <stdexcept>
#include "../registered_lab/contract.hpp"
namespace {
FILE *output = stdout;
struct KeepAwake {
    EXECUTION_STATE previous = SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    ~KeepAwake() { if (previous) SetThreadExecutionState(previous); }
};
struct Topology { std::vector<DISPLAYCONFIG_PATH_INFO> paths; std::vector<DISPLAYCONFIG_MODE_INFO> modes; };
Topology query(UINT flags) {
    for (UINT attempt = 0; attempt < 4; ++attempt) {
        UINT paths = 0, modes = 0;
        auto status = GetDisplayConfigBufferSizes(flags, &paths, &modes);
        if (status || paths > 64 || modes > 256) throw std::runtime_error("display topology size unavailable");
        Topology value; value.paths.resize(paths); value.modes.resize(modes);
        status = QueryDisplayConfig(flags, &paths, value.paths.data(), &modes, value.modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER) continue;
        if (status) throw std::runtime_error("display topology query failed");
        value.paths.resize(paths); value.modes.resize(modes); return value;
    }
    throw std::runtime_error("display topology did not stabilize");
}
bool equal(LUID a, LUID b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }
bool same_path(const DISPLAYCONFIG_PATH_INFO &a, const DISPLAYCONFIG_PATH_INFO &b) {
    return equal(a.sourceInfo.adapterId, b.sourceInfo.adapterId) && a.sourceInfo.id == b.sourceInfo.id &&
           equal(a.targetInfo.adapterId, b.targetInfo.adapterId) && a.targetInfo.id == b.targetInfo.id;
}
bool contains(const Topology &current, const DISPLAYCONFIG_PATH_INFO &path) {
    for (const auto &p : current.paths) if ((p.flags & DISPLAYCONFIG_PATH_ACTIVE) && same_path(p, path)) return true;
    return false;
}
bool position_preserved(const Topology &before, const Topology &after) {
    for (const auto &a : before.paths) {
        bool found = false;
        for (const auto &b : after.paths) if (same_path(a, b)) {
            const auto i = a.sourceInfo.modeInfoIdx, j = b.sourceInfo.modeInfoIdx;
            if (i >= before.modes.size() || j >= after.modes.size()) return false;
            const auto &x = before.modes[i], &y = after.modes[j];
            if (x.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE || y.infoType != x.infoType ||
                x.sourceMode.position.x != y.sourceMode.position.x || x.sourceMode.position.y != y.sourceMode.position.y ||
                x.sourceMode.width != y.sourceMode.width || x.sourceMode.height != y.sourceMode.height) return false;
            found = true;
        }
        if (!found) return false;
    }
    return true;
}
void print(const char *phase, const Topology &value) {
    std::fprintf(output, "{\"phase\":\"%s\",\"path_count\":%zu,\"mode_count\":%zu}\n", phase, value.paths.size(), value.modes.size());
    for (const auto &path : value.paths) std::fprintf(output,
        "{\"phase\":\"%s\",\"source_low\":%lu,\"source_high\":%ld,\"source_id\":%u,"
        "\"target_low\":%lu,\"target_high\":%ld,\"target_id\":%u,\"technology\":%u,\"available\":%s,\"active\":%s}\n",
        phase, path.sourceInfo.adapterId.LowPart, path.sourceInfo.adapterId.HighPart, path.sourceInfo.id,
        path.targetInfo.adapterId.LowPart, path.targetInfo.adapterId.HighPart, path.targetInfo.id,
        UINT(path.targetInfo.outputTechnology), path.targetInfo.targetAvailable ? "true" : "false",
        (path.flags & DISPLAYCONFIG_PATH_ACTIVE) ? "true" : "false");
    std::fflush(output);
}
}
int run_probe(bool sole_indirect = false, UINT observation_seconds = 45) {
    if (!nr_fill_guest()) return 2;
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || !session) return 2;
    WCHAR station[64]{}; DWORD bytes = 0;
    if (!GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &bytes) ||
        lstrcmpiW(station, L"WinSta0")) return 2;
    KeepAwake awake;
    if (!awake.previous) return 2;
    Topology before; bool restore = false, passed = false;
    try {
        BOOL screen_saver = FALSE;
        if (!SystemParametersInfoW(SPI_GETSCREENSAVERRUNNING, 0, &screen_saver, 0))
            throw std::runtime_error("screen-saver state unavailable");
        const auto input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        if (!input) throw std::runtime_error("input desktop unavailable");
        WCHAR input_name[64]{}; DWORD needed = 0;
        const bool default_input = GetUserObjectInformationW(input, UOI_NAME, input_name, sizeof(input_name), &needed) &&
            !lstrcmpiW(input_name, L"Default");
        CloseDesktop(input);
        std::fprintf(output, "{\"event\":\"interactive_state\",\"default_input_desktop\":%s,\"screen_saver_running\":%s,\"display_power_requested\":true}\n",
            default_input ? "true" : "false", screen_saver ? "true" : "false");
        std::fflush(output);
        // A topology can stay active while DWM stops composing a blanked or
        // secure desktop. KeepAwake does not dismiss an existing screen saver.
        if (!default_input || screen_saver) throw std::runtime_error("active Default desktop required for composition observation");
        // Adapter startup precedes asynchronous D3D preparation and monitor
        // arrival. Wait for a usable target before judging its topology.
        const auto deadline = GetTickCount64() + 30000;
        bool ready = false;
        while (!ready) {
            for (const auto &path : query(QDC_ALL_PATHS).paths)
                if (path.targetInfo.targetAvailable &&
                    path.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED)
                    ready = true;
            if (ready) break;
            if (GetTickCount64() >= deadline) throw std::runtime_error("indirect monitor arrival timed out");
            Sleep(250);
        }
        before = query(QDC_ONLY_ACTIVE_PATHS); print("before", before);
        if (before.paths.size() == 1 && !sole_indirect) {
            // Monitor arrival and automatic activation are separate updates.
            // v55 observed one active path here and two in the next query;
            // constructing a second extension from the stale snapshot failed.
            const auto activation_deadline = GetTickCount64() + 5000;
            while (GetTickCount64() < activation_deadline) {
                const auto active = query(QDC_ONLY_ACTIVE_PATHS);
                if (!position_preserved(before, active))
                    throw std::runtime_error("original display changed during monitor arrival");
                if (active.paths.size() == 2) {
                    before = active; print("activated_after_arrival", before); break;
                }
                if (active.paths.size() != 1) throw std::runtime_error("unexpected active display count");
                Sleep(250);
            }
        }
        if (before.paths.size() == 2 && !sole_indirect) {
            UINT indirect = 0;
            for (const auto &path : before.paths)
                if (path.targetInfo.targetAvailable && path.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED)
                    ++indirect;
            if (indirect != 1) throw std::runtime_error("one original and one active indirect display required");
            std::fprintf(output, "{\"automatic_activation_observed\":true,\"topology_changed_by_probe\":false,\"observation_seconds\":%u}\n", observation_seconds);
            std::fflush(output);
            for (UINT i = 0; i < observation_seconds; ++i) Sleep(1000);
            const auto current = query(QDC_ONLY_ACTIVE_PATHS); print("observed", current);
            if (current.paths.size() != before.paths.size() || !position_preserved(before, current))
                throw std::runtime_error("automatically activated topology changed during observation");
            std::fputs("{\"passed\":true,\"desktop_offload_tested\":false,\"render_device_verified\":false}\n", output);
            return 0;
        }
        const auto all = query(QDC_ALL_PATHS); print("available", all);
        if (before.paths.size() != 1 && !(sole_indirect && before.paths.size() == 2))
            throw std::runtime_error("one original display and at most one indirect display required");
        UINT original_path = UINT(before.paths.size()), originals = 0, active_indirect = 0;
        for (UINT i = 0; i < before.paths.size(); ++i) {
            if (before.paths[i].targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED) ++active_indirect;
            else { original_path = i; ++originals; }
        }
        if (originals != 1 || active_indirect > 1) throw std::runtime_error("one original non-indirect display required");
        DISPLAYCONFIG_PATH_INFO target{}; UINT candidates = 0;
        for (const auto &path : all.paths) if (path.targetInfo.targetAvailable &&
            path.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED) {
            target = path; ++candidates;
        }
        if (candidates != 1 || (!sole_indirect && contains(before, target)))
            throw std::runtime_error("one available indirect monitor required");
        // Supply the exact additional path: a remembered EXTEND topology may
        // successfully return without including a newly arrived indirect target.
        auto desired = before;
        const auto original = before.paths[original_path].sourceInfo.modeInfoIdx;
        if (original >= before.modes.size() || before.modes[original].infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
            throw std::runtime_error("original source mode unavailable");
        const auto &source = before.modes[original].sourceMode;
        if (sole_indirect) { desired.paths.clear(); desired.modes.clear(); }
        DISPLAYCONFIG_MODE_INFO mode{};
        mode.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
        mode.id = target.sourceInfo.id; mode.adapterId = target.sourceInfo.adapterId;
        mode.sourceMode.width = 1024; mode.sourceMode.height = 768;
        mode.sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
        mode.sourceMode.position = sole_indirect ? POINTL{0, 0} : POINTL{source.position.x + LONG(source.width), source.position.y};
        target.sourceInfo.modeInfoIdx = UINT(desired.modes.size());
        target.targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        target.targetInfo.refreshRate = {60, 1};
        target.targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
        target.targetInfo.scaling = DISPLAYCONFIG_SCALING_IDENTITY;
        target.targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
        target.flags = DISPLAYCONFIG_PATH_ACTIVE;
        desired.modes.push_back(mode); desired.paths.push_back(target);
        std::fprintf(output, "{\"sole_indirect_requested\":%s,\"automatic_restoration\":true}\n", sole_indirect ? "true" : "false");
        constexpr UINT flags = SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES;
        const auto validation = SetDisplayConfig(UINT(desired.paths.size()), desired.paths.data(),
            UINT(desired.modes.size()), desired.modes.data(), flags | SDC_VALIDATE);
        std::fprintf(output, "{\"operation\":\"validate\",\"win32_status\":%ld}\n", validation); std::fflush(output);
        if (validation) throw std::runtime_error("supplied indirect display topology invalid");
        const auto status = SetDisplayConfig(UINT(desired.paths.size()), desired.paths.data(),
            UINT(desired.modes.size()), desired.modes.data(), flags | SDC_APPLY);
        std::fprintf(output, "{\"operation\":\"extend\",\"win32_status\":%ld}\n", status); std::fflush(output);
        restore = true; // Restore even if Windows partially applied a failing request.
        if (status) throw std::runtime_error("display extension failed");
        auto active = query(QDC_ONLY_ACTIVE_PATHS);
        for (UINT attempt = 0; attempt < 20 && !contains(active, target); ++attempt) {
            Sleep(1000); active = query(QDC_ONLY_ACTIVE_PATHS);
        }
        print("extended", active);
        if (!contains(active, target) || (sole_indirect ? active.paths.size() != 1 : !position_preserved(before, active)))
            throw std::runtime_error("indirect display activation or original-display preservation failed");
        std::fprintf(output, "{\"indirect_display_active\":true,\"original_display_preserved\":%s,\"observation_seconds\":%u}\n", sole_indirect ? "false" : "true", observation_seconds);
        std::fflush(output);
        for (UINT i = 0; i < observation_seconds; ++i) Sleep(1000);
        passed = true;
    } catch (const std::exception &e) { std::fprintf(output, "{\"error\":\"%s\"}\n", e.what()); }
    if (restore) {
        const auto status = SetDisplayConfig(UINT(before.paths.size()), before.paths.data(), UINT(before.modes.size()),
            before.modes.data(), SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_APPLY | SDC_ALLOW_CHANGES);
        std::fprintf(output, "{\"operation\":\"restore\",\"win32_status\":%ld}\n", status);
        if (status) passed = false;
        else try {
            const auto current = query(QDC_ONLY_ACTIVE_PATHS); print("restored", current);
            if (current.paths.size() != before.paths.size() || !position_preserved(before, current)) passed = false;
        } catch (...) { passed = false; }
    }
    std::fprintf(output, "{\"passed\":%s,\"desktop_offload_tested\":false,\"render_device_verified\":false}\n", passed ? "true" : "false");
    return passed ? 0 : 1;
}

#ifndef NR_TOPOLOGY_GUI
int main() { return run_probe(); }
#else
// Direct interactive scheduled-task entry avoids console-host initialization.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    if (!nr_fill_guest()) return 2;
    int argc = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!args) return 2;
    bool sole_indirect = false; UINT observation_seconds = 45; int next = 3;
    if (argc > next && !lstrcmpW(args[next], L"--sole-indirect")) { sole_indirect = true; ++next; }
    bool duration_valid = true;
    if (argc == next + 2 && !lstrcmpW(args[next], L"--seconds")) {
        wchar_t *end = nullptr; const auto seconds = wcstoul(args[next + 1], &end, 10);
        duration_valid = end != args[next + 1] && !*end && seconds >= 5 && seconds <= 120;
        if (duration_valid) observation_seconds = UINT(seconds);
        next += 2;
    }
    bool valid = argc >= 3 && argc == next && duration_valid && !lstrcmpW(args[1], L"--report") &&
        !wcsncmp(args[2], L"idd-topology-", 13) && wcslen(args[2]) < 100;
    if (valid) for (const WCHAR *p = args[2]; *p; ++p)
        if (!((*p >= L'a' && *p <= L'z') || (*p >= L'0' && *p <= L'9') || *p == L'-')) valid = false;
    WCHAR path[MAX_PATH]{};
    if (valid) swprintf_s(path, L"C:\\npu-lab\\%s.jsonl", args[2]);
    LocalFree(args);
    if (!valid) return 2;
    const auto handle = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return 2;
    const auto fd = _open_osfhandle(intptr_t(handle), _O_WRONLY | _O_TEXT);
    if (fd < 0) { CloseHandle(handle); return 2; }
    output = _fdopen(fd, "w");
    if (!output) { _close(fd); return 2; }
    setvbuf(output, nullptr, _IONBF, 0);
    std::fprintf(output, "{\"event\":\"probe_started\",\"pid\":%lu,\"console_required\":false}\n", GetCurrentProcessId());
    const auto result = run_probe(sole_indirect, observation_seconds);
    fclose(output); return result;
}
#endif
