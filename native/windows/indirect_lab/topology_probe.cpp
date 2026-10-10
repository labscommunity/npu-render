// Finite interactive-desktop activation, with automatic restoration of prior paths.
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <vector>
#include <stdexcept>
#include "../registered_lab/contract.hpp"
namespace {
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
    std::printf("{\"phase\":\"%s\",\"path_count\":%zu,\"mode_count\":%zu}\n", phase, value.paths.size(), value.modes.size());
    for (const auto &path : value.paths) std::printf(
        "{\"phase\":\"%s\",\"source_low\":%lu,\"source_high\":%ld,\"source_id\":%u,"
        "\"target_low\":%lu,\"target_high\":%ld,\"target_id\":%u,\"technology\":%u,\"available\":%s,\"active\":%s}\n",
        phase, path.sourceInfo.adapterId.LowPart, path.sourceInfo.adapterId.HighPart, path.sourceInfo.id,
        path.targetInfo.adapterId.LowPart, path.targetInfo.adapterId.HighPart, path.targetInfo.id,
        UINT(path.targetInfo.outputTechnology), path.targetInfo.targetAvailable ? "true" : "false",
        (path.flags & DISPLAYCONFIG_PATH_ACTIVE) ? "true" : "false");
    std::fflush(stdout);
}
}
int main() {
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
        before = query(QDC_ONLY_ACTIVE_PATHS); print("before", before);
        const auto all = query(QDC_ALL_PATHS); print("available", all);
        if (before.paths.size() != 1) throw std::runtime_error("exactly one original active display required");
        DISPLAYCONFIG_PATH_INFO target{}; UINT candidates = 0;
        for (const auto &path : all.paths) if (path.targetInfo.targetAvailable &&
            path.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED) {
            target = path; ++candidates;
        }
        if (candidates != 1 || contains(before, target)) throw std::runtime_error("one new inactive indirect monitor required");
        // Extend only within the owned guest. Do not save a new topology to the database.
        const auto status = SetDisplayConfig(0, nullptr, 0, nullptr, SDC_TOPOLOGY_EXTEND | SDC_APPLY);
        std::printf("{\"operation\":\"extend\",\"win32_status\":%ld}\n", status); std::fflush(stdout);
        restore = true; // Restore even if Windows partially applied a failing request.
        if (status) throw std::runtime_error("display extension failed");
        const auto active = query(QDC_ONLY_ACTIVE_PATHS); print("extended", active);
        if (!contains(active, target) || !position_preserved(before, active))
            throw std::runtime_error("indirect display activation or original-display preservation failed");
        std::puts("{\"indirect_display_active\":true,\"original_display_preserved\":true,\"observation_seconds\":45}");
        std::fflush(stdout);
        for (UINT i = 0; i < 45; ++i) Sleep(1000);
        passed = true;
    } catch (const std::exception &e) { std::fprintf(stderr, "topology probe failed: %s\n", e.what()); }
    if (restore) {
        const auto status = SetDisplayConfig(UINT(before.paths.size()), before.paths.data(), UINT(before.modes.size()),
            before.modes.data(), SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_APPLY | SDC_ALLOW_CHANGES);
        std::printf("{\"operation\":\"restore\",\"win32_status\":%ld}\n", status);
        if (status) passed = false;
        else try {
            const auto current = query(QDC_ONLY_ACTIVE_PATHS); print("restored", current);
            if (current.paths.size() != before.paths.size() || !position_preserved(before, current)) passed = false;
        } catch (...) { passed = false; }
    }
    std::printf("{\"passed\":%s,\"desktop_offload_tested\":false,\"render_device_verified\":false}\n", passed ? "true" : "false");
    return passed ? 0 : 1;
}
