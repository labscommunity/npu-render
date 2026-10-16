#pragma once
#include "lab_exchange_io.hpp"
#include "contract.hpp"
#include <bcrypt.h>
#include <vector>
namespace nr::registered_lab {
class LabNpuExchange {
    std::wstring directory;
    uint32_t sequence = 0;
    bool failed = false;
public:
    LabNpuExchange() {
        if (!nr_fill_guest()) throw std::runtime_error("external NPU execution is restricted to the owned guest");
        WCHAR path[MAX_PATH]{};
        const auto count = GetEnvironmentVariableW(L"NR_REGISTERED_LAB_EXCHANGE", path, MAX_PATH);
        const std::wstring prefix = L"C:\\npu-lab\\exchange-";
        if (!count || count >= MAX_PATH) throw std::runtime_error("explicit laboratory exchange directory required");
        directory = path;
        if (directory.size() != prefix.size() + 32 || directory.compare(0, prefix.size(), prefix) ||
            directory.substr(prefix.size()).find_first_not_of(L"0123456789abcdef") != std::wstring::npos)
            throw std::runtime_error("invalid laboratory exchange directory");
        for (const auto &p : {std::wstring(L"C:\\npu-lab"), directory}) {
            const auto a = GetFileAttributesW(p.c_str());
            if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("laboratory exchange directory is not owned regular storage");
        }
        exchange::File claim(directory + L"\\client.lock", true);
        const DWORD pid = GetCurrentProcessId(); claim.write(&pid, sizeof(pid)); claim.flush();
    }
    std::vector<uint8_t> compose(uint32_t width, uint32_t height, const std::vector<uint8_t> &background,
                                  const std::vector<uint8_t> &foreground) {
        if (failed) throw std::runtime_error("external NPU exchange failed previously");
        try {
            exchange::Header request{};
            request.width = width; request.height = height; request.sequence = ++sequence;
            const uint64_t bytes = uint64_t(width) * height * 4;
            if (bytes > exchange::max_frame_bytes || background.size() != bytes || foreground.size() != bytes)
                throw std::runtime_error("invalid external NPU frame extent");
            request.frame_bytes = uint32_t(bytes);
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(request.nonce), sizeof(request.nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
                !exchange::request_valid(request, sizeof(request) + bytes * 2))
                throw std::runtime_error("external NPU request preparation failed");
            const auto suffix = std::to_wstring(sequence) + L".bin";
            const auto temporary = directory + L"\\request-" + suffix + L".tmp";
            const auto ready = directory + L"\\request-" + suffix;
            {
                exchange::File file(temporary, true);
                file.write(&request, sizeof(request)); file.write(background.data(), request.frame_bytes);
                file.write(foreground.data(), request.frame_bytes); file.flush();
            }
            exchange::publish(temporary, ready);
            const auto reply = directory + L"\\reply-" + suffix;
            const auto started = GetTickCount64();
            while (GetFileAttributesW(reply.c_str()) == INVALID_FILE_ATTRIBUTES) {
                if (GetLastError() != ERROR_FILE_NOT_FOUND || GetTickCount64() - started >= 120000)
                    throw std::runtime_error("external NPU reply unavailable or timed out");
                Sleep(25);
            }
            exchange::File file(reply, false); exchange::Header response{};
            const auto size = file.size();
            if (size != sizeof(response) + bytes) throw std::runtime_error("external NPU reply size mismatch");
            file.read(&response, sizeof(response));
            if (!exchange::response_valid(response, request, size)) throw std::runtime_error("external NPU reply identity mismatch");
            std::vector<uint8_t> pixels(request.frame_bytes); file.read(pixels.data(), request.frame_bytes);
            return pixels;
        } catch (...) { failed = true; throw; }
    }
};
}
