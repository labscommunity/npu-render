#pragma once
#include "indexed_exchange_protocol.hpp"
#include "lab_exchange_io.hpp"
#include "contract.hpp"
#include <bcrypt.h>
namespace nr::registered_lab {
inline bool indexed_exchange_configured() {
    return nr_fill_guest() && GetFileAttributesW(L"C:\\npu-lab\\indexed-exchange-active.txt") != INVALID_FILE_ATTRIBUTES;
}
class IndexedNpuExchange {
    std::wstring directory;
    uint32_t sequence = 0;
    bool failed = false;
public:
    IndexedNpuExchange() {
        if (!nr_fill_guest()) throw std::runtime_error("indexed NPU transport requires the owned guest");
        exchange::File config(L"C:\\npu-lab\\indexed-exchange-active.txt", false);
        const std::string prefix = "C:\\npu-lab\\indexed-exchange-";
        indexed_exchange::require(config.size() == prefix.size() + 32);
        std::string path(size_t(config.size()), '\0'); config.read(path.data(), DWORD(path.size()));
        indexed_exchange::require(path.compare(0, prefix.size(), prefix) == 0 && path.substr(prefix.size()).find_first_not_of("0123456789abcdef") == std::string::npos);
        directory.assign(path.begin(), path.end());
        for (const auto &p : {std::wstring(L"C:\\npu-lab"), directory}) {
            const auto a = GetFileAttributesW(p.c_str());
            indexed_exchange::require(a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT));
        }
    }
    raster::UniformDrawExecutor::Result execute(indexed_exchange::Request request) {
        if (failed) throw std::runtime_error("indexed NPU transport previously failed");
        try {
            auto &h = request.header; h.sequence = ++sequence; h.client_pid = GetCurrentProcessId();
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(h.nonce), sizeof(h.nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                throw std::runtime_error("indexed NPU nonce generation failed");
            const auto bytes = indexed_exchange::encode(request);
            wchar_t suffix[100]{}; swprintf_s(suffix, L"-%u-%016llx%016llx-%u.bin", h.client_pid,
                static_cast<unsigned long long>(h.nonce[0]), static_cast<unsigned long long>(h.nonce[1]), h.sequence);
            const auto ready = directory + L"\\request" + suffix, temporary = ready + L".tmp";
            { exchange::File file(temporary, true); file.write(bytes.data(), DWORD(bytes.size())); file.flush(); }
            exchange::publish(temporary, ready);
            const auto reply = directory + L"\\reply" + suffix; const auto started = GetTickCount64();
            while (GetFileAttributesW(reply.c_str()) == INVALID_FILE_ATTRIBUTES) {
                if (GetLastError() != ERROR_FILE_NOT_FOUND || GetTickCount64() - started >= 90000)
                    throw std::runtime_error("indexed NPU reply unavailable or timed out");
                Sleep(10);
            }
            exchange::File file(reply, false); const auto size = file.size();
            indexed_exchange::require(size >= sizeof(indexed_exchange::Header) && size <= indexed_exchange::max_packet_bytes);
            std::vector<uint8_t> response(size_t(size), 0); file.read(response.data(), DWORD(response.size()));
            return indexed_exchange::decode_response(response, h);
        } catch (...) { failed = true; throw; }
    }
};
}
