#include "local_indexed_client.hpp"
#include <thread>
#include <cstdio>
#include <exception>
namespace ex = nr::registered_lab::indexed_exchange;
namespace stream = nr::registered_lab::indexed_stream;
using Client = nr::registered_lab::LocalIndexedClient;
void check(bool value) { if (!value) throw std::runtime_error("native client contract failed"); }
int main() try {
    ex::Request request; auto &h = request.header;
    h.abi = 13; h.flags = 0x200000; h.nonce[0] = 7; h.client_pid = GetCurrentProcessId();
    h.sequence = 1; h.width = h.height = 2; h.format = 87; request.clear_color = {0,0,0,1};
    const auto bytes = ex::encode(request);
    nr::raster::UniformDrawExecutor::Result result;
    result.pixels.assign(4, {0,0,0,255}); result.execution_device = "NPU"; // Synthetic peer, no device execution.
    result.input_bytes = result.output_bytes = 16; result.covered_samples = 4;
    result.provider_requests = result.tile_executions = 1;
    const auto valid = ex::response(h, result);
    const char *labels[]{"valid-reply", "wrong-nonce", "wrong-device", "reserved-receipt", "excess-coverage", "invalid-input-count", "zero-provider-count", "short-pixel-payload", "provider-failure", "pending-read-timeout", "pending-read-cancellation"};
    for (unsigned kind = 0; kind < 11; ++kind) {
        wchar_t suffix[40]{}; swprintf_s(suffix, L"%016llx%016llx", static_cast<unsigned long long>(GetCurrentProcessId()), static_cast<unsigned long long>(kind));
        const auto name = std::wstring(L"\\\\.\\pipe\\npu-render-indexed-") + suffix;
        stream::Handle pipe(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 512, 1000, nullptr));
        stream::Handle cancel(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        std::exception_ptr server_error, client_error; bool acknowledged = false, rejected = false, poisoned = false;
        std::thread peer([&] {
            try {
                const auto deadline = GetTickCount64() + 3000;
                stream::Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); OVERLAPPED operation{}; operation.hEvent = event.value;
                const auto ready = ConnectNamedPipe(pipe.value, &operation); const auto error = ready ? ERROR_SUCCESS : GetLastError();
                if (error != ERROR_PIPE_CONNECTED) (void)stream::completed(pipe.value, operation, ready, error, deadline);
                check(stream::read_packet(pipe.value, deadline) == bytes);
                if (kind >= 9) { if (kind == 10) { Sleep(20); SetEvent(cancel.value); } Sleep(150); return; }
                auto reply = valid; auto rh = ex::Reader(reply).value<ex::Header>();
                ex::Receipt receipt{}; std::memcpy(&receipt, reply.data() + sizeof(rh), sizeof(receipt));
                if (kind == 1) ++rh.nonce[0];
                if (kind == 2) rh.device = 2;
                if (kind == 3) receipt.reserved = 1;
                if (kind == 4) receipt.covered_samples = 5;
                if (kind == 5) receipt.input_bytes = 0;
                if (kind == 6) receipt.provider_requests = 0;
                std::memcpy(reply.data(), &rh, sizeof(rh));
                std::memcpy(reply.data() + sizeof(rh), &receipt, sizeof(receipt));
                if (kind == 7) reply.pop_back();
                if (kind == 8) { rh.kind = 3; rh.device = 0; rh.body_bytes = 0; reply.resize(sizeof(rh)); std::memcpy(reply.data(), &rh, sizeof(rh)); }
                stream::write_packet(pipe.value, reply, deadline);
                try { uint32_t ack = 0; stream::transfer(pipe.value, &ack, 4, false, deadline); check(ack == h.sequence); acknowledged = true; }
                catch (const stream::PipeError &e) { if (!e.client_departure()) throw; }
            } catch (...) { server_error = std::current_exception(); }
        });
        try {
            Client client(name, GetCurrentProcess(), kind == 9 ? 50u : 1000u);
            const auto began = GetTickCount64();
            try { check(client.exchange(bytes, cancel.value) == valid); }
            catch (const std::exception &) { rejected = true; }
            check(rejected == (kind != 0));
            if (kind >= 9) check(GetTickCount64() - began < 2000);
            if (rejected) {
                try { (void)client.exchange(bytes); }
                catch (const std::runtime_error &e) { poisoned = !std::strcmp(e.what(), "local NPU client previously failed"); }
                check(poisoned);
            }
        } catch (...) { client_error = std::current_exception(); }
        peer.join();
        if (server_error) std::rethrow_exception(server_error);
        if (client_error) std::rethrow_exception(client_error);
        check(acknowledged == (kind == 0));
        std::printf("{\"case\":\"%s\",\"passed\":true,\"synthetic_peer\":true,\"delivery_acknowledged\":%s,\"client_poisoned\":%s}\n", labels[kind], acknowledged ? "true" : "false", poisoned ? "true" : "false");
    }
    const auto name = std::wstring(L"\\\\.\\pipe\\npu-render-indexed-00000000000000000000000000000000");
    for (unsigned kind = 0; kind < 4; ++kind) {
        bool rejected = false;
        try { Client client(kind == 0 ? L"invalid" : name, GetCurrentProcess(), kind == 1 ? 0u : kind == 2 ? 90001u : 1u);
            auto invalid = bytes; auto header = h; ++header.client_pid; std::memcpy(invalid.data(), &header, sizeof(header)); (void)client.exchange(invalid); }
        catch (const std::exception &) { rejected = true; }
        check(rejected);
    }
    std::printf("{\"native_client_contract\":true,\"cases\":15,\"passed\":true,\"device_execution_tested\":false}\n");
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
