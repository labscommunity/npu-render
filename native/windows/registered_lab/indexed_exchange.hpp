#pragma once
#include "indexed_exchange_protocol.hpp"
#include "lab_exchange_io.hpp"
#include "contract.hpp"
#include "indexed_pipe.hpp"
#include <bcrypt.h>
#ifdef NR_NATIVE_PROBE
#include "native_probe_lease.hpp"
#include "native_auto_admission.hpp"
#include "local_indexed_client.hpp"
#include "lab_trace.hpp"
#endif
namespace nr::registered_lab {
inline bool indexed_exchange_configured() {
#ifdef NR_NATIVE_PROBE
    // Device creation needs the lease, not a live worker: with the worker gone the device runs
    // on the CPU fallback and the exchange's own strict check fails per request instead.
    try { nr::native_probe::Admission admission(nr::auto_admission::read_or_request()); admission.verify(false); return true; }
    catch (...) { return false; }
#else
    return nr_fill_guest() && (GetFileAttributesW(L"C:\\npu-lab\\indexed-exchange-active.txt") != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(L"C:\\npu-lab\\indexed-pipe-active.txt") != INVALID_FILE_ATTRIBUTES);
#endif
}
class IndexedNpuExchange {
    std::wstring directory, pipe;
    uint32_t sequence = 0;
    bool failed = false;
#ifdef NR_NATIVE_PROBE
    std::unique_ptr<nr::native_probe::Admission> admission;
    std::unique_ptr<LocalIndexedClient> local_client;
    unsigned failovers = 0;
    static constexpr uint64_t failover_wait_ms = 30000;
    // Worker failover. The controller replaces a worker that ended (a crash, a restart) by
    // publishing a new one: this process's lease is rewritten with the new worker's identity,
    // or, for an automatically admitted client, the resident broker publishes again. The
    // request that found the old worker gone waits for that here and binds to the replacement
    // instead of poisoning the device; only a second failure does (trial 23: a worker's end
    // used to cost DWM its device).
    bool recover(const char *what, const char *error) {
        const auto started = GetTickCount64();
        lab_trace("{\"event\":\"exchange_failover_started\",\"request\":\"%s\",\"error\":\"%s\"}\n", what, error);
        for (unsigned attempt = 0; GetTickCount64() - started < failover_wait_ms; ++attempt) {
            Sleep(attempt ? 250 : 50);
            try {
                auto fresh = std::make_unique<nr::native_probe::Admission>(nr::auto_admission::read_or_request());
                auto client = std::make_unique<LocalIndexedClient>(fresh->pipe_name(), fresh->worker_handle(), 90000);
                admission = std::move(fresh); local_client = std::move(client); ++failovers;
                lab_trace("{\"event\":\"exchange_failover\",\"request\":\"%s\",\"worker_pid\":%lu,\"after_ms\":%llu,\"failovers\":%u}\n",
                          what, GetProcessId(admission->worker_handle()), static_cast<unsigned long long>(GetTickCount64() - started), failovers);
                return true;
            } catch (const std::exception &) {}
        }
        lab_trace("{\"event\":\"exchange_failover_failed\",\"request\":\"%s\",\"after_ms\":%llu}\n", what, static_cast<unsigned long long>(GetTickCount64() - started));
        return false;
    }
    // One request through the current worker; when it fails, bind to the replacement worker and
    // send it once more. The retry's failure propagates and poisons the transport as before.
    std::vector<uint8_t> send(const std::vector<uint8_t> &bytes, const char *what) {
        try { admission->verify(); return local_client->exchange(bytes); }
        catch (const std::exception &first) {
            if (!recover(what, first.what())) throw;
            admission->verify(); return local_client->exchange(bytes);
        }
    }
#endif
public:
    IndexedNpuExchange() {
#ifdef NR_NATIVE_PROBE
        admission = std::make_unique<nr::native_probe::Admission>(nr::auto_admission::read_or_request());
        local_client = std::make_unique<LocalIndexedClient>(admission->pipe_name(), admission->worker_handle(), 90000);
#else
        if (!nr_fill_guest()) throw std::runtime_error("indexed NPU transport requires the owned guest");
        if (GetFileAttributesW(L"C:\\npu-lab\\indexed-pipe-active.txt") != INVALID_FILE_ATTRIBUTES) {
            indexed_exchange::require(GetFileAttributesW(L"C:\\npu-lab\\indexed-exchange-active.txt") == INVALID_FILE_ATTRIBUTES);
            exchange::File config(L"C:\\npu-lab\\indexed-pipe-active.txt", false);
            indexed_exchange::require(config.size() < 128); std::string value(size_t(config.size()), '\0');
            config.read(value.data(), DWORD(value.size())); pipe.assign(value.begin(), value.end());
            indexed_exchange::require(indexed_stream::pipe_name_valid(pipe)); return;
        }
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
#endif
    }
    raster::UniformDrawExecutor::Result execute(indexed_exchange::Request request) {
        if (failed) throw std::runtime_error("indexed NPU transport previously failed");
        try {
            auto &h = request.header; h.sequence = ++sequence; h.client_pid = GetCurrentProcessId();
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(h.nonce), sizeof(h.nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                throw std::runtime_error("indexed NPU nonce generation failed");
            const auto bytes = indexed_exchange::encode(request);
#ifdef NR_NATIVE_PROBE
            auto result = indexed_exchange::decode_response(send(bytes, "draw"), h);
            result.execution_device = "NPU (local indexed)";
            return result;
#else
            if (!pipe.empty()) return indexed_exchange::decode_response(indexed_stream::exchange_packet(pipe, bytes), h);
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
#endif
        } catch (...) { failed = true; throw; }
    }
    // Frame batch (abi 24): sends the header and body as one packet and returns the validated reply.
    std::vector<uint8_t> exchange_batch(indexed_exchange::Header h, const std::vector<uint8_t> &body) {
        if (failed) throw std::runtime_error("indexed NPU transport previously failed");
        try {
            h.sequence = ++sequence; h.client_pid = GetCurrentProcessId(); h.abi = 24; h.kind = 1; h.device = 0; h.body_bytes = uint32_t(body.size());
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(h.nonce), sizeof(h.nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                throw std::runtime_error("indexed NPU nonce generation failed");
            std::vector<uint8_t> bytes(sizeof(h) + body.size());
            std::memcpy(bytes.data(), &h, sizeof(h)); std::memcpy(bytes.data() + sizeof(h), body.data(), body.size());
            indexed_exchange::require(indexed_exchange::frame_batch_valid(h, bytes.size()));
#ifdef NR_NATIVE_PROBE
            auto reply = send(bytes, "frame_batch");
#else
            indexed_exchange::require(!pipe.empty());
            auto reply = indexed_stream::exchange_packet(pipe, bytes);
#endif
            const auto rh = indexed_exchange::Reader(reply).value<indexed_exchange::Header>();
            if (indexed_exchange::failure_valid(rh, h, reply.size())) throw std::runtime_error("NPU worker rejected the frame batch");
            indexed_exchange::require(indexed_exchange::frame_batch_response_valid(rh, h, reply.size()));
            return reply;
        } catch (...) { failed = true; throw; }
    }
};
}
