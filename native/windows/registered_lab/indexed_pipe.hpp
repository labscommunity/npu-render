#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "indexed_exchange_protocol.hpp"
namespace nr::registered_lab::indexed_stream {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) { if (!h || h == INVALID_HANDLE_VALUE) throw std::runtime_error("indexed pipe handle unavailable"); }
    ~Handle() { CloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
inline DWORD remaining(ULONGLONG deadline) {
    const auto now = GetTickCount64();
    if (now >= deadline) throw std::runtime_error("indexed stream deadline expired");
    return DWORD((std::min)(deadline - now, ULONGLONG(300000)));
}
inline DWORD completed(HANDLE handle, OVERLAPPED &operation, BOOL immediate, DWORD error, ULONGLONG deadline, DWORD immediate_bytes = 0, HANDLE cancel = nullptr) {
    if (immediate) return immediate_bytes;
    if (!immediate && error != ERROR_IO_PENDING) throw std::runtime_error("indexed pipe operation failed: " + std::to_string(error));
    // Drain cancellation before the stack-owned OVERLAPPED or its event is freed.
    DWORD wait = WAIT_FAILED;
    try {
        const HANDLE events[]{operation.hEvent,cancel};
        wait = cancel ? WaitForMultipleObjects(2,events,FALSE,remaining(deadline)) : WaitForSingleObject(operation.hEvent, remaining(deadline));
    } catch (...) {
        CancelIoEx(handle, &operation); DWORD ignored = 0; GetOverlappedResult(handle, &operation, &ignored, TRUE); throw;
    }
    if (wait != WAIT_OBJECT_0) {
        CancelIoEx(handle, &operation); DWORD ignored = 0; GetOverlappedResult(handle, &operation, &ignored, TRUE);
        throw std::runtime_error("indexed pipe wait expired");
    }
    DWORD actual = 0;
    if (!GetOverlappedResult(handle, &operation, &actual, FALSE)) throw std::runtime_error("indexed pipe completion failed");
    return actual;
}
inline void transfer(HANDLE pipe, void *data, DWORD bytes, bool write, ULONGLONG deadline, HANDLE cancel = nullptr) {
    auto p = static_cast<uint8_t *>(data); DWORD cursor = 0;
    while (cursor < bytes) {
        if(cancel && WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)throw std::runtime_error("indexed pipe cancelled");
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); OVERLAPPED operation{}; operation.hEvent = event.value;
        const DWORD count = (std::min)(bytes - cursor, DWORD(64 * 1024));
        DWORD immediate_bytes = 0;
        const BOOL ready = write ? WriteFile(pipe, p + cursor, count, &immediate_bytes, &operation) : ReadFile(pipe, p + cursor, count, &immediate_bytes, &operation);
        const auto error = ready ? ERROR_SUCCESS : GetLastError();
        const auto actual = completed(pipe, operation, ready, error, deadline, immediate_bytes,cancel);
        if (!actual || actual > count) throw std::runtime_error("indexed pipe truncated transfer"); cursor += actual;
    }
}
inline std::vector<uint8_t> read_packet(HANDLE pipe, ULONGLONG deadline, HANDLE cancel = nullptr) {
    uint32_t size = 0; transfer(pipe, &size, 4, false, deadline,cancel);
    indexed_exchange::require(size >= sizeof(indexed_exchange::Header) && size <= indexed_exchange::max_packet_bytes);
    std::vector<uint8_t> bytes(size); transfer(pipe, bytes.data(), size, false, deadline,cancel); return bytes;
}
inline void write_packet(HANDLE pipe, const std::vector<uint8_t> &bytes, ULONGLONG deadline, HANDLE cancel = nullptr) {
    indexed_exchange::require(bytes.size() >= sizeof(indexed_exchange::Header) && bytes.size() <= indexed_exchange::max_packet_bytes);
    auto size = uint32_t(bytes.size()); transfer(pipe, &size, 4, true, deadline,cancel);
    transfer(pipe, const_cast<uint8_t *>(bytes.data()), size, true, deadline,cancel);
}
inline bool pipe_name_valid(const std::wstring &name) {
    const std::wstring prefix = L"\\\\.\\pipe\\npu-render-indexed-";
    return name.size() == prefix.size() + 32 && name.compare(0, prefix.size(), prefix) == 0 &&
        name.substr(prefix.size()).find_first_not_of(L"0123456789abcdef") == std::wstring::npos;
}
inline std::vector<uint8_t> exchange_packet(const std::wstring &name, const std::vector<uint8_t> &bytes) {
    indexed_exchange::require(pipe_name_valid(name)); const auto deadline = GetTickCount64() + 90000;
    HANDLE opened = INVALID_HANDLE_VALUE;
    while (opened == INVALID_HANDLE_VALUE) {
        opened = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (opened != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        if (error != ERROR_PIPE_BUSY || !WaitNamedPipeW(name.c_str(), remaining(deadline))) throw std::runtime_error("indexed NPU pipe unavailable");
    }
    Handle pipe(opened); write_packet(pipe.value, bytes, deadline); auto reply = read_packet(pipe.value, deadline);
    const auto request = indexed_exchange::Reader(bytes).value<indexed_exchange::Header>();
    const auto response = indexed_exchange::Reader(reply).value<indexed_exchange::Header>();
    indexed_exchange::require(indexed_exchange::response_valid(response, request, reply.size()) || indexed_exchange::failure_valid(response, request, reply.size()));
    auto sequence = request.sequence; transfer(pipe.value, &sequence, 4, true, deadline); return reply;
}
}
