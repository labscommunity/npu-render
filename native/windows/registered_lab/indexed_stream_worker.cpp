#include "indexed_socket.hpp"
#include "indexed_pipeline.hpp"
#include "provider_compile_statistics.hpp"
#include <cstdio>
namespace stream = nr::registered_lab::indexed_stream;
namespace ex = nr::registered_lab::indexed_exchange;
int wmain(int argc, wchar_t **argv) try {
    if (argc != 3) throw std::invalid_argument("expected <private token file> <maximum requests>; connects only to loopback port 49406");
    wchar_t *end = nullptr; const auto limit = std::wcstoul(argv[2], &end, 10);
    ex::require(end != argv[2] && !*end && limit && limit <= ex::max_requests);
    stream::Winsock startup; stream::Socket socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)); stream::nonblocking(socket.value);
    const auto deadline = GetTickCount64() + 600000;
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(49406); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket.value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
        throw std::runtime_error("indexed loopback connect failed");
    stream::wait_socket(socket.value, POLLWRNORM, GetTickCount64() + 15000);
    int error = 0, size = sizeof(error);
    if (getsockopt(socket.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size) || error) throw std::runtime_error("indexed loopback connection refused");
    stream::authenticate(socket.value, stream::secret(argv[1]), false, GetTickCount64() + 15000);
    ex::Pipeline pipeline;uint32_t captured=0;
    for (uint32_t count = 0; count < limit && GetTickCount64() < deadline; ++count) {
        const auto bytes = stream::socket_read(socket.value, deadline); ex::Header h{};
        std::memcpy(&h, bytes.data(), sizeof(h)); ex::require(ex::request_valid(h, bytes.size()));
        std::vector<uint8_t> reply;const auto draw_started=GetTickCount64();const auto cache_before=nr::provider::compile_statistics();
        try {
            const auto request = ex::decode(bytes); const auto start = GetTickCount64();
            const auto result = pipeline.execute(request); reply = ex::response(h, result);
            std::printf("{\"sequence\":%u,\"client_pid\":%u,\"execution_device\":\"%s\",\"operation\":\"%s\",\"width\":%u,\"height\":%u,\"textured\":%s,\"vertices\":%u,\"triangle_batch\":%u,\"provider_requests\":%llu,\"covered_samples\":%llu,\"tile_executions\":%llu,\"draw_ms\":%.3f,\"compile_and_draw_ms\":%llu,\"displayed_frames\":0,\"passed\":true}\n", h.sequence, h.client_pid, result.execution_device.c_str(), ex::color_clear(h)?"color_clear":"draw", h.width, h.height, (h.flags & 2) ? "true" : "false",h.vertices,(h.flags&0x3000)?ex::triangle_batch(request,nr::shader::RegisterExecutor::preferred_raster_batch(request.pixel)):0u,
                static_cast<unsigned long long>(result.provider_requests),static_cast<unsigned long long>(result.covered_samples),static_cast<unsigned long long>(result.tile_executions), result.timings.total_ms, static_cast<unsigned long long>(GetTickCount64() - start));
        } catch (const std::exception &e) {
            std::fprintf(stderr, "indexed stream draw rejected: %.32000s\n", e.what());
            auto failed = h; failed.kind = 3; failed.body_bytes = 0; failed.device = 0; ex::Writer writer; writer.value(failed); reply = writer.finish();
            // Unsupported draw state must not discard already qualified models.
            // Provider/runtime failures reset the cache before another request.
            if (!dynamic_cast<const std::invalid_argument *>(&e)) pipeline = ex::Pipeline{};
        }
        // Preserve a bounded sample of real slow draws for offline diagnosis.
        // Files contain only public draw-packet data, never the stream secret.
        if(h.client_pid!=42 && captured<4 && GetTickCount64()-draw_started>=5000){
            const auto ordinal=++captured;bool saved=false;
            try {
                const std::wstring token_path=argv[1];const auto separator=token_path.find_last_of(L"\\/");
                ex::require(separator!=std::wstring::npos);
                const auto path=token_path.substr(0,separator+1)+L"slow-request-"+std::to_wstring(ordinal)+L".bin";
                nr::registered_lab::exchange::File capture(path,true);capture.write(bytes.data(),DWORD(bytes.size()));capture.flush();saved=true;
            } catch(const std::exception &) {}
            std::printf("{\"kind\":\"slow_draw_packet\",\"file\":\"slow-request-%u.bin\",\"client_pid\":%u,\"bytes\":%zu,\"saved\":%s}\n",ordinal,h.client_pid,bytes.size(),saved?"true":"false");
        }
        const auto cache_after=nr::provider::compile_statistics();
        if(cache_after.calls!=cache_before.calls)std::printf("{\"kind\":\"model_compile_cache\",\"client_pid\":%u,\"sequence\":%u,\"compile_calls\":%llu,\"cache_requests\":%llu,\"cache_hits\":%llu,\"cache_status_unavailable\":%llu}\n",
            h.client_pid,h.sequence,static_cast<unsigned long long>(cache_after.calls-cache_before.calls),static_cast<unsigned long long>(cache_after.cache_requests-cache_before.cache_requests),
            static_cast<unsigned long long>(cache_after.cache_hits-cache_before.cache_hits),static_cast<unsigned long long>(cache_after.cache_status_unavailable-cache_before.cache_status_unavailable));
        std::fflush(stdout); std::fflush(stderr); stream::socket_write(socket.value, reply, (std::min)(deadline, GetTickCount64() + 90000));
    }
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
