#include "indexed_socket.hpp"
#include "contract.hpp"
#include <sddl.h>
#include <cstdio>
namespace stream = nr::registered_lab::indexed_stream;
namespace ex = nr::registered_lab::indexed_exchange;
int wmain(int argc, wchar_t **argv) try {
    if (argc != 4 || !nr_fill_guest()) throw std::invalid_argument("owned guest and <pipe name> <private token file> <maximum requests> required");
    wchar_t *end = nullptr; const auto limit = std::wcstoul(argv[3], &end, 10);
    ex::require(end != argv[3] && !*end && limit && limit <= ex::max_requests);
    const std::wstring name = argv[1]; ex::require(stream::pipe_name_valid(name));
    stream::Winsock startup; stream::Socket listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    const BOOL exclusive = TRUE;
    if (setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive), sizeof(exclusive))) throw std::runtime_error("relay socket exclusivity failed");
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(49406); address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener.value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) || listen(listener.value, 1)) throw std::runtime_error("relay listen failed");
    const auto deadline = GetTickCount64() + 300000;
    stream::wait_socket(listener.value, POLLRDNORM, deadline);
    stream::Socket socket(accept(listener.value, nullptr, nullptr)); stream::nonblocking(socket.value);
    stream::authenticate(socket.value, stream::secret(argv[2]), true, GetTickCount64() + 15000);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    // Per-job random name, local clients only. Config is read-only for clients.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)", SDDL_REVISION_1, &descriptor, nullptr)) throw std::runtime_error("relay pipe ACL failed");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    HANDLE raw = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 1000, &attributes);
    LocalFree(descriptor); stream::Handle pipe(raw);
    std::puts("{\"relay_ready\":true,\"rendering_performed\":false}"); std::fflush(stdout);
    for (uint32_t count = 0; count < limit && GetTickCount64() < deadline; ++count) {
        stream::Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); OVERLAPPED operation{}; operation.hEvent = event.value;
        const BOOL ready = ConnectNamedPipe(pipe.value, &operation); const auto error = ready ? ERROR_SUCCESS : GetLastError();
        if (error != ERROR_PIPE_CONNECTED) (void)stream::completed(pipe.value, operation, ready, error, deadline);
        try {
            const auto started = GetTickCount64(); const auto request_deadline = (std::min)(deadline, started + 90000);
            const auto bytes = stream::read_packet(pipe.value, request_deadline); ex::Header h{}; std::memcpy(&h, bytes.data(), sizeof(h));
            ex::require(ex::request_valid(h, bytes.size()));
            stream::socket_write(socket.value, bytes, request_deadline); const auto reply = stream::socket_read(socket.value, request_deadline);
            const auto response = ex::Reader(reply).value<ex::Header>();
            ex::require(ex::response_valid(response, h, reply.size()) || ex::failure_valid(response, h, reply.size()));
            stream::write_packet(pipe.value, reply, request_deadline);
            // Client acknowledges receipt before DisconnectNamedPipe can discard queued data.
            uint32_t acknowledged = 0; stream::transfer(pipe.value, &acknowledged, 4, false, request_deadline); ex::require(acknowledged == h.sequence);
            std::printf("{\"sequence\":%u,\"client_pid\":%u,\"request_bytes\":%zu,\"reply_bytes\":%zu,\"roundtrip_ms\":%llu,\"worker_success\":%s,\"displayed_frames\":0}\n", h.sequence, h.client_pid, bytes.size(), reply.size(),
                static_cast<unsigned long long>(GetTickCount64() - started), response.kind == 2 ? "true" : "false"); std::fflush(stdout);
        } catch (...) { DisconnectNamedPipe(pipe.value); throw; }
        if (!DisconnectNamedPipe(pipe.value)) throw std::runtime_error("relay pipe disconnect failed");
    }
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
