#include "indexed_socket.hpp"
#include "indexed_pipeline.hpp"
#include <cstdio>
namespace stream = nr::registered_lab::indexed_stream;
namespace ex = nr::registered_lab::indexed_exchange;
int wmain(int argc, wchar_t **argv) try {
    if (argc != 3) throw std::invalid_argument("expected <private token file> <maximum requests>; connects only to loopback port 49406");
    wchar_t *end = nullptr; const auto limit = std::wcstoul(argv[2], &end, 10);
    ex::require(end != argv[2] && !*end && limit && limit <= ex::max_requests);
    stream::Winsock startup; stream::Socket socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)); stream::nonblocking(socket.value);
    const auto deadline = GetTickCount64() + 300000;
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(49406); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket.value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
        throw std::runtime_error("indexed loopback connect failed");
    stream::wait_socket(socket.value, POLLWRNORM, GetTickCount64() + 15000);
    int error = 0, size = sizeof(error);
    if (getsockopt(socket.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size) || error) throw std::runtime_error("indexed loopback connection refused");
    stream::authenticate(socket.value, stream::secret(argv[1]), false, GetTickCount64() + 15000);
    ex::Pipeline pipeline;
    for (uint32_t count = 0; count < limit && GetTickCount64() < deadline; ++count) {
        const auto bytes = stream::socket_read(socket.value, deadline); ex::Header h{};
        std::memcpy(&h, bytes.data(), sizeof(h)); ex::require(ex::request_valid(h, bytes.size()));
        std::vector<uint8_t> reply;
        try {
            const auto request = ex::decode(bytes); const auto start = GetTickCount64();
            const auto result = pipeline.execute(request); reply = ex::response(h, result);
            std::printf("{\"sequence\":%u,\"client_pid\":%u,\"execution_device\":\"%s\",\"width\":%u,\"height\":%u,\"provider_requests\":%llu,\"draw_ms\":%.3f,\"compile_and_draw_ms\":%llu,\"displayed_frames\":0,\"passed\":true}\n", h.sequence, h.client_pid, result.execution_device.c_str(), h.width, h.height,
                static_cast<unsigned long long>(result.provider_requests), result.timings.total_ms, static_cast<unsigned long long>(GetTickCount64() - start));
        } catch (const std::exception &e) {
            std::fprintf(stderr, "indexed stream draw rejected: %.32000s\n", e.what());
            auto failed = h; failed.kind = 3; failed.body_bytes = 0; failed.device = 0; ex::Writer writer; writer.value(failed); reply = writer.finish();
            pipeline = ex::Pipeline{};
        }
        std::fflush(stdout); std::fflush(stderr); stream::socket_write(socket.value, reply, (std::min)(deadline, GetTickCount64() + 90000));
    }
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
