#pragma once
// Bounded diagnostics in the owned guest or a valid native lease; no display claims.
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include "contract.hpp"
#ifdef NR_NATIVE_PROBE
#include "native_probe_lease.hpp"
#include <memory>
#ifndef NR_LAB_TRACE_ROOT
#define NR_LAB_TRACE_ROOT LR"(C:\ProgramData\T8-NpuRender-NativeTraceV1)"
#endif
// Bytes per ring file of the per-process trace (three files are kept).
#ifndef NR_LAB_TRACE_FILE_BYTES
#define NR_LAB_TRACE_FILE_BYTES (16ull * 1024 * 1024)
#endif
#else
#define NR_LAB_TRACE_ROOT LR"(C:\npu-lab\umd-trace)"
#endif
namespace nr::registered_lab {
// The compositor can retain this DLL after a finite trial ends. Validate a
// fresh lease after the prior controller/worker expires; never retain a failed
// admission forever. The generation also retires the prior diagnostic handle.
inline uint64_t lab_trace_session() noexcept {
#ifdef NR_NATIVE_PROBE
    try {
        static std::mutex admission_mutex;
        std::lock_guard<std::mutex> lock(admission_mutex);
        static std::unique_ptr<nr::native_probe::Admission> admission;
        static uint64_t generation = 0;
        if (admission) {
            // The trace never connects to the worker: a consumer keeps its lease while the
            // controller replaces a lost worker, and so does its trace (same rule as device creation).
            try { admission->verify(false); return generation; }
            catch (...) { admission.reset(); }
        }
        admission = std::make_unique<nr::native_probe::Admission>(nr::native_probe::read_mapping());
        return ++generation;
    } catch (...) { return 0; }
#else
    static const bool enabled = nr_fill_guest(); return enabled ? 1 : 0;
#endif
}
inline bool lab_trace_enabled() noexcept { return lab_trace_session() != 0; }

inline void lab_trace(const char *format, ...) noexcept {
    const auto session = lab_trace_session();
    if (!session) return;
    static std::mutex trace_mutex;
    std::lock_guard<std::mutex> lock(trace_mutex);
    // Opening the file for every DDI enters the filesystem filter stack on each
    // call. Retain one handle per loaded UMD. The trace is a ring of files:
    // process-<pid>.jsonl, process-<pid>.1.jsonl, .2.jsonl, ... each capped at
    // NR_LAB_TRACE_FILE_BYTES; when one fills the next file opens and the file two
    // behind it is deleted, so a process keeps at most three files (the earlier
    // single 4 MB cap hid the later minutes of every probe longer than ten minutes).
    // A reloaded UMD continues from the highest file that exists for its process.
    struct TraceFile {
        HANDLE handle = INVALID_HANDLE_VALUE;
        uint64_t bytes = 0, generation = 0;
        UINT lines = 0, file = 0;
        static void name(WCHAR *path, UINT file) noexcept {
            if (file) swprintf_s(path, MAX_PATH, NR_LAB_TRACE_ROOT L"\\process-%lu.%u.jsonl", GetCurrentProcessId(), file);
            else swprintf_s(path, MAX_PATH, NR_LAB_TRACE_ROOT L"\\process-%lu.jsonl", GetCurrentProcessId());
        }
        // Opens ring file `which`, appending to it; its current length counts against the cap.
        void open(UINT which) noexcept {
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE; bytes = 0; lines = 0; file = which;
            WCHAR path[MAX_PATH]{}; name(path, which);
            handle = CreateFileW(path, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                 nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            LARGE_INTEGER size{};
            if (handle != INVALID_HANDLE_VALUE && GetFileSizeEx(handle, &size) && size.QuadPart >= 0)
                bytes = uint64_t(size.QuadPart);
            else bytes = NR_LAB_TRACE_FILE_BYTES;
        }
        void begin(uint64_t session) noexcept {
            generation = session;
            // Continue from the highest ring file this process already has.
            UINT highest = 0;
            for (UINT which = 1; which < 100000; ++which) {
                WCHAR path[MAX_PATH]{}; name(path, which);
                if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) break;
                highest = which;
            }
            open(highest);
        }
        // The current file is full: open the next one and drop the file three behind it.
        void rotate() noexcept {
            const UINT next = file + 1;
            open(next);
            if (next >= 3) { WCHAR path[MAX_PATH]{}; name(path, next - 3); DeleteFileW(path); }
        }
        ~TraceFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    };
    static TraceFile trace;
    // A delayed call from an older verified lease cannot reopen its old epoch.
    if (session < trace.generation) return;
    if (session != trace.generation) trace.begin(session);
    if (trace.handle != INVALID_HANDLE_VALUE && trace.bytes > NR_LAB_TRACE_FILE_BYTES - 2048) trace.rotate();
    if (trace.handle == INVALID_HANDLE_VALUE || trace.bytes > NR_LAB_TRACE_FILE_BYTES - 2048) return;
    const auto file = trace.handle;
    if (!trace.lines) {
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
    ++trace.lines;
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
    if (!lab_trace_enabled() || !code || code[1] < 2 || code[1] > 16384) return;
    // Preserve at most eight rejected programs per process across DLL reloads.
    // The filename hash deduplicates diagnostics; it is not an integrity check.
    uint64_t hash = 14695981039346656037ull;
    for (UINT i = 0; i < code[1]; ++i) { hash ^= code[i]; hash *= 1099511628211ull; }
    WCHAR path[MAX_PATH]{}, pattern[MAX_PATH]{};
    swprintf_s(path, NR_LAB_TRACE_ROOT L"\\shader-%lu-%u-%016llx.bin", GetCurrentProcessId(), kind, hash);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    swprintf_s(pattern, NR_LAB_TRACE_ROOT L"\\shader-%lu-*.bin", GetCurrentProcessId());
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
