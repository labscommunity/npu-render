#pragma once
// Bounded diagnostics in the owned guest only; no display or execution claims.
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include "contract.hpp"
namespace nr::registered_lab {
inline void lab_trace(const char *format, ...) noexcept {
    static const bool enabled = nr_fill_guest();
    if (!enabled) return;
    static std::mutex trace_mutex;
    std::lock_guard<std::mutex> lock(trace_mutex);
    WCHAR path[MAX_PATH]{};
    swprintf_s(path, L"C:\\npu-lab\\umd-trace\\process-%lu.jsonl", GetCurrentProcessId());
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart < 4 * 1024 * 1024) {
        char line[1024]{};
        va_list args; va_start(args, format);
        const auto length = _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
        va_end(args);
        DWORD written = 0;
        if (length > 0) WriteFile(file, line, DWORD(length), &written, nullptr);
    }
    CloseHandle(file);
}
}
