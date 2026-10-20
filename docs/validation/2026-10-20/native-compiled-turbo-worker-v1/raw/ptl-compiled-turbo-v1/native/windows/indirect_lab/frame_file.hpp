#pragma once
#include "frame_readback.hpp"
namespace nr::indirect {
struct FrameHeader {
    uint64_t magic = 0x31454d415246524eull; // NRFRAME1, little endian
    uint32_t version = 1, header_bytes = 64;
    uint64_t sequence = 0;
    uint32_t width = 0, height = 0, format = 0, row_bytes = 0;
    uint64_t pixel_bytes = 0, qpc = 0;
    uint32_t producer_pid = 0, reserved = 0;
};
static_assert(sizeof(FrameHeader) == 64);
inline HRESULT write_frame_file(const WCHAR *temporary, const WCHAR *destination,
                                const FramePixels &frame, uint64_t sequence) noexcept {
    if (!temporary || !destination || !sequence || !frame.width || !frame.height ||
        frame.width > 8192 || frame.height > 8192 || uint64_t(frame.width) * frame.height > 4 * 1024 * 1024 ||
        (frame.format != DXGI_FORMAT_B8G8R8A8_UNORM && frame.format != DXGI_FORMAT_R8G8B8A8_UNORM) ||
        frame.bytes.size() != size_t(frame.width) * frame.height * 4 || !lstrcmpW(temporary, destination))
        return E_INVALIDARG;
    FrameHeader header; header.sequence = sequence; header.width = frame.width; header.height = frame.height;
    header.format = UINT(frame.format); header.row_bytes = frame.width * 4;
    header.pixel_bytes = frame.bytes.size(); header.producer_pid = GetCurrentProcessId();
    LARGE_INTEGER stamp{}; if (!QueryPerformanceCounter(&stamp)) return E_FAIL;
    header.qpc = uint64_t(stamp.QuadPart);
    const auto file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    DWORD written = 0;
    HRESULT status = S_OK;
    if (!WriteFile(file, &header, sizeof(header), &written, nullptr)) status = HRESULT_FROM_WIN32(GetLastError());
    else if (written != sizeof(header)) status = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    if (SUCCEEDED(status)) {
        if (!WriteFile(file, frame.bytes.data(), DWORD(frame.bytes.size()), &written, nullptr)) status = HRESULT_FROM_WIN32(GetLastError());
        else if (written != frame.bytes.size()) status = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }
    if (!CloseHandle(file) && SUCCEEDED(status)) status = HRESULT_FROM_WIN32(GetLastError());
    // Readers open with FILE_SHARE_DELETE; they retain the previous complete
    // file while the next complete frame atomically replaces the directory entry.
    if (SUCCEEDED(status) && !ReplaceFileW(destination, temporary, nullptr, 0, nullptr, nullptr)) {
        const auto error = GetLastError();
        // ReplaceFile supports an existing reader's shared-delete handle;
        // MoveFileEx(REPLACE_EXISTING) returned access denied in that case.
        // Only the first publication has no destination. Do not overwrite a
        // concurrently created destination through this first-file path.
        if (error != ERROR_FILE_NOT_FOUND || !MoveFileExW(temporary, destination, 0))
            status = HRESULT_FROM_WIN32(error == ERROR_FILE_NOT_FOUND ? GetLastError() : error);
    }
    return status;
}
}
