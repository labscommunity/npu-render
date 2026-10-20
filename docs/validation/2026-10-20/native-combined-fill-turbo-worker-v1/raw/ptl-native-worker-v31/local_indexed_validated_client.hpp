#pragma once
#include "indexed_pipe.hpp"
#include "response_envelope_v1.hpp"
#include <mutex>

namespace nr::registered_lab {
// A transport client, not authority to load a physical graphics adapter. The
// controller supplies a handle to its known worker, never a discovered pipe PID.
class LocalIndexedValidatedClient {
    std::wstring name;
    indexed_stream::Handle server;
    DWORD server_pid, timeout_ms;
    std::mutex mutex;
    bool failed = false;

    static HANDLE pin(HANDLE process) {
        HANDLE copy = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &copy,
                             SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0))
            throw std::runtime_error("local NPU worker handle unavailable");
        return copy;
    }
    void active(HANDLE cancellation) const {
        if (WaitForSingleObject(server.value, 0) != WAIT_TIMEOUT)
            throw std::runtime_error("local NPU worker is no longer running");
        if (cancellation && WaitForSingleObject(cancellation, 0) != WAIT_TIMEOUT)
            throw std::runtime_error("local NPU request cancelled");
    }
public:
    LocalIndexedValidatedClient(std::wstring endpoint, HANDLE owned_worker, DWORD milliseconds)
        : name(std::move(endpoint)), server(pin(owned_worker)),
          server_pid(GetProcessId(server.value)), timeout_ms(milliseconds) {
        indexed_exchange::require(indexed_stream::pipe_name_valid(name) && server_pid &&
                                  timeout_ms && timeout_ms <= 90000);
        active(nullptr);
    }
    LocalIndexedValidatedClient(const LocalIndexedValidatedClient &) = delete;
    LocalIndexedValidatedClient &operator=(const LocalIndexedValidatedClient &) = delete;

    std::vector<uint8_t> exchange(const std::vector<uint8_t> &bytes, HANDLE cancellation = nullptr) {
        std::lock_guard<std::mutex> lock(mutex);
        if (failed) throw std::runtime_error("local NPU client previously failed");
        try {
            const auto request = indexed_exchange::Reader(bytes).value<indexed_exchange::Header>();
            indexed_exchange::require(indexed_exchange::request_valid(request, bytes.size()) &&
                                      request.client_pid == GetCurrentProcessId());
            const auto deadline = GetTickCount64() + timeout_ms;
            HANDLE opened = INVALID_HANDLE_VALUE;
            while (opened == INVALID_HANDLE_VALUE) {
                active(cancellation);
                (void)indexed_stream::remaining(deadline);
                opened = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT |
                                     SECURITY_IDENTIFICATION, nullptr);
                if (opened != INVALID_HANDLE_VALUE) break;
                const auto error = GetLastError();
                if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND)
                    throw indexed_stream::PipeError("local NPU pipe unavailable", error);
                const DWORD delay = (std::min)(indexed_stream::remaining(deadline), DWORD(10));
                if (error == ERROR_PIPE_BUSY) (void)WaitNamedPipeW(name.c_str(), delay);
                else Sleep(delay);
            }
            indexed_stream::Handle pipe(opened);
            ULONG actual_pid = 0;
            if (!GetNamedPipeServerProcessId(pipe.value, &actual_pid) || actual_pid != server_pid)
                throw std::runtime_error("local NPU server identity mismatch");
            active(cancellation);
            indexed_stream::write_packet(pipe.value, bytes, deadline, cancellation);
            auto reply = indexed_stream::read_packet(pipe.value, deadline, cancellation);
            // Validate the full reply, including receipt and pixel extent, before
            // acknowledging delivery. A failed exchange permanently poisons this client.
            (void)indexed_exchange::validate_response_envelope(reply, request);
            active(cancellation);
            auto sequence = request.sequence;
            indexed_stream::transfer(pipe.value, &sequence, sizeof(sequence), true, deadline, cancellation);
            return reply;
        } catch (...) { failed = true; throw; }
    }
};
}
