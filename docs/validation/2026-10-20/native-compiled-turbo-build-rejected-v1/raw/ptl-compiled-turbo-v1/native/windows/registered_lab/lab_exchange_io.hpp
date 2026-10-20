#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdexcept>
#include <string>
#include "lab_exchange_protocol.hpp"
namespace nr::registered_lab::exchange {
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    File(const std::wstring &path, bool write) {
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("exchange reparse point rejected");
        handle = CreateFileW(path.c_str(), write ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ, nullptr,
                             write ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("exchange file unavailable");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
            CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
            throw std::runtime_error("exchange requires a regular file");
        }
    }
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    uint64_t size() const {
        LARGE_INTEGER value{};
        if (!GetFileSizeEx(handle, &value) || value.QuadPart < 0) throw std::runtime_error("exchange file size unavailable");
        return uint64_t(value.QuadPart);
    }
    void read(void *out, DWORD bytes) {
        DWORD actual = 0;
        if (!ReadFile(handle, out, bytes, &actual, nullptr) || actual != bytes) throw std::runtime_error("incomplete exchange read");
    }
    void write(const void *data, DWORD bytes) {
        DWORD actual = 0;
        if (!WriteFile(handle, data, bytes, &actual, nullptr) || actual != bytes) throw std::runtime_error("incomplete exchange write");
    }
    void flush() { if (!FlushFileBuffers(handle)) throw std::runtime_error("exchange flush failed"); }
};
inline void publish(const std::wstring &temporary, const std::wstring &ready) {
    // Never replace existing evidence or expose a partially written request.
    if (!MoveFileExW(temporary.c_str(), ready.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("exchange publication failed");
}
}
