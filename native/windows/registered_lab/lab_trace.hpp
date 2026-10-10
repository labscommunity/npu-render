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
    static UINT lines = 0;
    // Opening the file for every DDI enters the filesystem filter stack on each
    // call. Retain one handle per loaded UMD and stop I/O once its budget is used.
    // Read the existing length at load time so device/DLL retries share the cap.
    struct TraceFile {
        HANDLE handle = INVALID_HANDLE_VALUE;
        uint64_t bytes = 0;
        TraceFile() noexcept {
            WCHAR path[MAX_PATH]{};
            swprintf_s(path, L"C:\\npu-lab\\umd-trace\\process-%lu.jsonl", GetCurrentProcessId());
            handle = CreateFileW(path, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                 nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            LARGE_INTEGER size{};
            if (handle != INVALID_HANDLE_VALUE && GetFileSizeEx(handle, &size) && size.QuadPart >= 0)
                bytes = uint64_t(size.QuadPart);
            else bytes = 4 * 1024 * 1024;
        }
        ~TraceFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    };
    static TraceFile trace;
    if (lines >= 4096 || trace.handle == INVALID_HANDLE_VALUE || trace.bytes > 4 * 1024 * 1024 - 2048) return;
    const auto file = trace.handle;
    if (!lines) {
        WCHAR module[MAX_PATH]{};
        const auto count = GetModuleFileNameW(nullptr, module, MAX_PATH);
        const WCHAR *name = module;
        if (count && count < MAX_PATH) {
            for (const WCHAR *p = module; *p; ++p) if (*p == L'\\') name = p + 1;
        } else name = L"unknown";
        char safe[MAX_PATH]{};
        for (UINT i = 0; name[i] && i + 1 < MAX_PATH; ++i) {
            const auto c = name[i];
            safe[i] = ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                (c >= L'0' && c <= L'9') || c == L'.' || c == L'-' || c == L'_') ? char(c) : '?';
        }
        char header[512]{};
        const auto length = sprintf_s(header, "{\"event\":\"process\",\"pid\":%lu,\"image\":\"%s\"}\n", GetCurrentProcessId(), safe);
        DWORD written = 0;
        if (length > 0) {
            WriteFile(file, header, DWORD(length), &written, nullptr);
            trace.bytes += written;
        }
    }
    ++lines;
    {
        char line[1024]{};
        va_list args; va_start(args, format);
        const auto length = _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
        va_end(args);
        DWORD written = 0;
        if (length > 0) {
            WriteFile(file, line, DWORD(length), &written, nullptr);
            trace.bytes += written;
        }
    }
}
inline void lab_shader_capture(UINT kind, const UINT *code) noexcept {
    if (!nr_fill_guest() || !code || code[1] < 2 || code[1] > 16384) return;
    // Preserve at most eight rejected programs per process across DLL reloads.
    // The filename hash deduplicates diagnostics; it is not an integrity check.
    uint64_t hash = 14695981039346656037ull;
    for (UINT i = 0; i < code[1]; ++i) { hash ^= code[i]; hash *= 1099511628211ull; }
    WCHAR path[MAX_PATH]{}, pattern[MAX_PATH]{};
    swprintf_s(path, L"C:\\npu-lab\\umd-trace\\shader-%lu-%u-%016llx.bin", GetCurrentProcessId(), kind, hash);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    swprintf_s(pattern, L"C:\\npu-lab\\umd-trace\\shader-%lu-*.bin", GetCurrentProcessId());
    WIN32_FIND_DATAW entry{}; UINT count = 0;
    const auto search = FindFirstFileW(pattern, &entry);
    if (search != INVALID_HANDLE_VALUE) {
        do { ++count; } while (count < 8 && FindNextFileW(search, &entry));
        FindClose(search);
    }
    if (count >= 8) return;
    const auto file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0; const DWORD bytes = code[1] * sizeof(UINT);
    const bool saved = WriteFile(file, code, bytes, &written, nullptr) && written == bytes;
    CloseHandle(file);
    lab_trace("{\"event\":\"rejected_shader_capture\",\"kind\":%u,\"words\":%u,\"hash\":\"%016llx\",\"saved\":%s}\n",
              kind, code[1], hash, saved ? "true" : "false");
}
}
