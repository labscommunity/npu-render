#pragma once
#include "window_capture.hpp"
#include <dwmapi.h>
#include <string>
namespace nr::capture {
struct Entry {
    HWND hwnd{};
    DWORD pid{};
    RECT rect{}, bounds{};
    std::string name;
};
inline std::vector<Entry> enumerate_sources() {
    std::vector<Entry> entries;
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            if (!IsWindowVisible(h) || IsIconic(h))
                return TRUE;
            DWORD cloak = 0;
            DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloak, sizeof(cloak));
            if (cloak)
                return TRUE;
            RECT r{};
            if (!GetWindowRect(h, &r) || r.right <= r.left || r.bottom <= r.top)
                return TRUE;
            RECT screen{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)},
                intersection{};
            if (!IntersectRect(&intersection, &r, &screen))
                return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid == GetCurrentProcessId())
                return TRUE;
            char cls[256]{};
            GetClassNameA(h, cls, 256);
            RECT bounds = r;
            DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds));
            reinterpret_cast<std::vector<Entry> *>(p)->push_back({h, pid, r, bounds, cls});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&entries));
    if (entries.size() > 64)
        throw std::runtime_error("desktop source count exceeds validated resource bound");
    uint64_t total_pixels = 0;
    for (const auto &e : entries) {
        uint64_t largest = 0;
        for (const auto &r : {e.rect, e.bounds}) {
            const auto width = int64_t(r.right) - r.left, height = int64_t(r.bottom) - r.top;
            if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
                throw std::runtime_error("source dimensions exceed capture bound");
            largest = std::max(largest, uint64_t(width * height));
        }
        total_pixels += largest;
    }
    if (total_pixels > 32ull * 1024 * 1024)
        throw std::runtime_error("aggregate source pixels exceed capture bound");
    return entries;
}
inline bool input_desktop_ready() {
    auto desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desk)
        return false;
    wchar_t name[128]{};
    DWORD needed = 0;
    bool result = GetUserObjectInformationW(desk, UOI_NAME, name, sizeof(name), &needed) &&
                  _wcsicmp(name, L"Default") == 0;
    CloseDesktop(desk);
    return result;
}
inline RECT source_bounds(const Entry &entry, const Image &image) {
    for (const auto &r : {entry.bounds, entry.rect})
        if (UINT(r.right - r.left) == image.width && UINT(r.bottom - r.top) == image.height)
            return r;
    throw std::runtime_error("capture dimensions do not match current physical window bounds");
}
} // namespace nr::capture
