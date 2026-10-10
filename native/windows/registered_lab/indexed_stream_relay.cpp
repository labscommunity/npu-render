#include "indexed_socket.hpp"
#include "contract.hpp"
#include "fifo_dispatch.hpp"
#include <sddl.h>
#include <atomic>
#include <memory>
#include <thread>
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
    const auto deadline = GetTickCount64() + 600000;
    stream::wait_socket(listener.value, POLLRDNORM, deadline);
    stream::Socket socket(accept(listener.value, nullptr, nullptr)); stream::nonblocking(socket.value);
    stream::authenticate(socket.value, stream::secret(argv[2]), true, GetTickCount64() + 15000);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    // Per-job random name, local clients only. Config is read-only for clients.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)", SDDL_REVISION_1, &descriptor, nullptr)) throw std::runtime_error("relay pipe ACL failed");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    constexpr DWORD clients=4;
    std::vector<std::unique_ptr<stream::Handle>> pipes;
    try {
        for(DWORD i=0;i<clients;++i){
            HANDLE raw=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (i?0:FILE_FLAG_FIRST_PIPE_INSTANCE),
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,clients,65536,65536,1000,&attributes);
            pipes.push_back(std::make_unique<stream::Handle>(raw));
        }
    } catch(...){LocalFree(descriptor);throw;}
    LocalFree(descriptor);
    stream::Handle stop_event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    nr::registered_lab::FifoDispatch dispatch{uint32_t(limit)};
    std::atomic<bool> stopping{false};std::atomic<uint32_t> acknowledged_count{0};
    std::mutex error_mutex;std::exception_ptr failure;
    const auto stop=[&]{
        stopping.store(true);SetEvent(stop_event.value);dispatch.cancel();shutdown(socket.value,SD_BOTH);
    };
    const auto fail=[&](std::exception_ptr error){
        {std::lock_guard<std::mutex> lock(error_mutex);if(!failure)failure=error;}
        stop();
    };
    const auto serve=[&](HANDLE pipe){
        try {
            while(!stopping.load() && GetTickCount64()<deadline){
                stream::Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED operation{};operation.hEvent=event.value;
                const BOOL ready=ConnectNamedPipe(pipe,&operation);const auto error=ready?ERROR_SUCCESS:GetLastError();
                if(error!=ERROR_PIPE_CONNECTED)(void)stream::completed(pipe,operation,ready,error,deadline,0,stop_event.value);
                const auto started=GetTickCount64();const auto request_deadline=(std::min)(deadline,started+90000);
                const auto bytes=stream::read_packet(pipe,request_deadline,stop_event.value);
                const auto h=ex::Reader(bytes).value<ex::Header>();ex::require(ex::request_valid(h,bytes.size()));
                uint32_t ticket=0;
                try{ticket=dispatch.reserve();}catch(const nr::registered_lab::FifoDispatch::Exhausted &){DisconnectNamedPipe(pipe);return;}
                const auto queued=GetTickCount64();ULONGLONG entered=0,finished=0;
                const auto turn_deadline=nr::registered_lab::FifoDispatch::Clock::now()+std::chrono::milliseconds(stream::remaining(request_deadline));
                const auto reply=dispatch.execute(ticket,turn_deadline,[&]{
                    entered=GetTickCount64();
                    stream::socket_write(socket.value,bytes,request_deadline);auto result=stream::socket_read(socket.value,request_deadline);
                    const auto response=ex::Reader(result).value<ex::Header>();
                    ex::require(ex::response_valid(response,h,result.size()) || ex::failure_valid(response,h,result.size()));
                    finished=GetTickCount64();return result;
                });
                const auto response=ex::Reader(reply).value<ex::Header>();
                // Release the provider turn before delivering this client's reply.
                stream::write_packet(pipe,reply,request_deadline,stop_event.value);
                uint32_t acknowledged=0;stream::transfer(pipe,&acknowledged,4,false,request_deadline,stop_event.value);ex::require(acknowledged==h.sequence);
                std::printf("{\"sequence\":%u,\"client_pid\":%u,\"dispatch_ticket\":%u,\"request_bytes\":%zu,\"reply_bytes\":%zu,\"queue_wait_ms\":%llu,\"worker_ms\":%llu,\"roundtrip_ms\":%llu,\"worker_success\":%s,\"displayed_frames\":0}\n",
                    h.sequence,h.client_pid,ticket,bytes.size(),reply.size(),static_cast<unsigned long long>(entered-queued),static_cast<unsigned long long>(finished-entered),
                    static_cast<unsigned long long>(GetTickCount64()-started),response.kind==2?"true":"false");std::fflush(stdout);
                if(!DisconnectNamedPipe(pipe))throw std::runtime_error("relay pipe disconnect failed");
                if(acknowledged_count.fetch_add(1)+1==limit){stop();return;}
            }
        }catch(...){if(!stopping.load())fail(std::current_exception());DisconnectNamedPipe(pipe);}
    };
    std::vector<std::thread> threads;
    try{for(const auto &pipe:pipes)threads.emplace_back(serve,pipe->value);}
    catch(...){fail(std::current_exception());}
    std::printf("{\"relay_ready\":true,\"concurrent_clients\":%lu,\"provider_concurrency\":1,\"rendering_performed\":false}\n",clients);std::fflush(stdout);
    for(auto &thread:threads)thread.join();
    if(failure)std::rethrow_exception(failure);
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
