// Finite private Windows named-pipe NPU worker; no driver installation or desktop admission.
#include "indexed_pipe.hpp"
#include "indexed_pipeline.hpp"
#include "packet_provider_process.hpp"
#include "provider_compile.hpp"
#include "provider_profile.hpp"
#include "coverage_statistics.hpp"
#include "fifo_dispatch.hpp"
#include "request_recording.hpp"
#include "native_compositor_peer.hpp"
#include <sddl.h>
#include <atomic>
#include <memory>
#include <thread>
#include <cstdio>
namespace stream = nr::registered_lab::indexed_stream;
namespace ex = nr::registered_lab::indexed_exchange;
namespace {

uint64_t qpc_now(){LARGE_INTEGER value{};ex::require(QueryPerformanceCounter(&value)!=0 && value.QuadPart>=0);return uint64_t(value.QuadPart);}
uint64_t qpc_frequency(){LARGE_INTEGER value{};ex::require(QueryPerformanceFrequency(&value)!=0 && value.QuadPart>0);return uint64_t(value.QuadPart);}
std::string shader_digest(const std::vector<uint32_t> &words){
    ex::require(words.size()<=ULONG_MAX/sizeof(uint32_t));BCRYPT_ALG_HANDLE algorithm=nullptr;
    ex::require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
    std::array<unsigned char,32> digest{};
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<uint32_t *>(words.data())),ULONG(words.size()*sizeof(uint32_t)),digest.data(),ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);ex::require(status>=0);
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(const auto byte:digest){result.push_back(hex[byte>>4]);result.push_back(hex[byte&15]);}return result;
}
std::wstring current_user_sid(){
    HANDLE raw=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))throw std::runtime_error("worker identity unavailable");stream::Handle token(raw);DWORD size=0;
    GetTokenInformation(token.value,TokenUser,nullptr,0,&size);if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || !size || size>65536)throw std::runtime_error("worker identity size rejected");
    std::vector<uint8_t> bytes(size);if(!GetTokenInformation(token.value,TokenUser,bytes.data(),size,&size))throw std::runtime_error("worker identity read failed");
    LPWSTR text=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid,&text))throw std::runtime_error("worker identity conversion failed");
    try{std::wstring result(text);LocalFree(text);return result;}catch(...){LocalFree(text);throw;}
}
}
void capture_failed_request(const std::vector<uint8_t> &bytes,const ex::Header &h,uint32_t ticket)noexcept{
    bool captured=false;wchar_t *path=nullptr;size_t length=0;
    if(!_wdupenv_s(&path,&length,L"NR_RENDER_FAILURE_PACKET") && path && *path && bytes.size()<=ex::max_packet_bytes){
        const auto file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE){DWORD written=0;captured=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(file);CloseHandle(file);}
    }
    std::free(path);std::printf("{\"provider_request_failed\":true,\"dispatch_ticket\":%u,\"client_pid\":%u,\"sequence\":%u,\"abi\":%u,\"width\":%u,\"height\":%u,\"vertices\":%u,\"flags\":%u,\"request_bytes\":%zu,\"failure_packet_captured\":%s}\n",ticket,h.client_pid,h.sequence,h.abi,h.width,h.height,h.vertices,h.flags,bytes.size(),captured?"true":"false");std::fflush(stdout);
}
int wmain(int argc, wchar_t **argv) try {
    if(argc!=4 && argc!=6 && argc!=7 && argc!=8)throw std::invalid_argument("expected <private pipe> <requests> <seconds> [compositor-pid creation-filetime]");
    wchar_t *end = nullptr; const auto limit = std::wcstoul(argv[2], &end, 10);
    ex::require(end != argv[2] && !*end && limit && limit <= ex::max_requests);
    const std::wstring name = argv[1]; ex::require(stream::pipe_name_valid(name));
    end=nullptr;const auto seconds=std::wcstoul(argv[3],&end,10);ex::require(end!=argv[3] && !*end && seconds>=60 && seconds<=2400);
    const auto frequency=qpc_frequency();
    const auto deadline=GetTickCount64()+uint64_t(seconds)*1000;
    std::unique_ptr<nr::registered_lab::NativeCompositorPeer> compositor;
    if(argc>=6)compositor=std::make_unique<nr::registered_lab::NativeCompositorPeer>(DWORD(nr::registered_lab::NativeCompositorPeer::number(argv[4],UINT32_MAX)),nr::registered_lab::NativeCompositorPeer::number(argv[5],UINT64_MAX),argc==7?1u:0u);
    std::unique_ptr<nr::registered_lab::NativeCompositorPeer> indirect;
    if(argc==8)indirect=std::make_unique<nr::registered_lab::NativeCompositorPeer>(DWORD(nr::registered_lab::NativeCompositorPeer::number(argv[6],UINT32_MAX)),nr::registered_lab::NativeCompositorPeer::number(argv[7],UINT64_MAX),2u);
    ex::require(argc!=7 || !std::wcscmp(argv[6],L"local-service-probe"));
    ex::Pipeline pipeline;nr::registered_lab::PacketProviderProcess provider;nr::registered_lab::RequestRecording recording;std::atomic<uint32_t> succeeded{0};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    // Private per-job endpoint: only this user and SYSTEM, local clients only.
    const auto worker_user=current_user_sid();
    char *interactive_setting=nullptr;size_t interactive_length=0;bool interactive_pipe=false;
    ex::require(!_dupenv_s(&interactive_setting,&interactive_length,"NR_RENDER_INTERACTIVE_PIPE"));
    if(interactive_setting){const std::string value(interactive_setting);std::free(interactive_setting);ex::require(value=="0"||value=="1");interactive_pipe=value=="1";}
    // Explicit opt-in for same-account interactive clients. Administrators own
    // the pipe. Ordinary clients receive data/attribute/synchronization rights,
    // never permission to create a server instance or alter its access policy.
    const auto prefix=interactive_pipe?L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100083;;;":L"D:P(A;;GA;;;SY)(A;;GA;;;";
    const auto acl=prefix+worker_user+L")"+(compositor?L"(A;;GA;;;"+compositor->sid()+L")":L"")+(indirect?L"(A;;GA;;;"+indirect->sid()+L")":L"")+(interactive_pipe?L"S:(ML;;NW;;;ME)":L"");
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) throw std::runtime_error("relay pipe ACL failed");
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
    if(interactive_pipe){std::printf("{\"interactive_pipe_policy\":true,\"enabled\":true,\"ordinary_client_access_mask\":1048707,\"ordinary_client_server_creation_allowed\":false}\n");std::fflush(stdout);}
    stream::Handle stop_event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    nr::registered_lab::FifoDispatch dispatch{uint32_t(limit)};
    std::atomic<bool> stopping{false};std::atomic<uint32_t> settled_count{0};
    std::mutex error_mutex;std::exception_ptr failure;
    const auto stop=[&]{
        stopping.store(true);SetEvent(stop_event.value);dispatch.cancel();
    };
    const auto fail=[&](std::exception_ptr error){
        {std::lock_guard<std::mutex> lock(error_mutex);if(!failure)failure=error;}
        stop();
    };
    const auto serve=[&](HANDLE pipe){
        try {
            while(!stopping.load() && GetTickCount64()<deadline){
                const char *phase="connect";ex::Header h{};uint32_t ticket=UINT32_MAX;bool dispatched=false;
                const auto departure=[&](DWORD code){
                    std::printf("{\"kind\":\"client_departure\",\"phase\":\"%s\",\"win32_error\":%lu,\"client_pid\":%u,\"sequence\":%u,\"dispatch_ticket\":%u,\"provider_response_received\":%s,\"delivery_acknowledged\":false}\n",
                        phase,code,h.client_pid,h.sequence,ticket,dispatched?"true":"false");std::fflush(stdout);
                };
                try {
                    stream::Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED operation{};operation.hEvent=event.value;
                    const BOOL ready=ConnectNamedPipe(pipe,&operation);const auto error=ready?ERROR_SUCCESS:GetLastError();
                    if(error!=ERROR_PIPE_CONNECTED)(void)stream::completed(pipe,operation,ready,error,deadline,0,stop_event.value);
                    const auto connected_qpc=qpc_now();const auto started=GetTickCount64();const auto request_deadline=(std::min)(deadline,started+90000);
                    phase="request";const auto bytes=stream::read_packet(pipe,request_deadline,stop_event.value);
                    const auto received_qpc=qpc_now();
                    h=ex::Reader(bytes).value<ex::Header>();ex::require(ex::request_valid(h,bytes.size()));
                    ULONG actual_client=0;ex::require(GetNamedPipeClientProcessId(pipe,&actual_client)!=0 && actual_client==h.client_pid);
                    if(indirect && actual_client==indirect->pid())indirect->verify();else if(compositor)compositor->authorize(actual_client,worker_user);
                    try{ticket=dispatch.reserve();}catch(const nr::registered_lab::FifoDispatch::Exhausted &){DisconnectNamedPipe(pipe);return;}
                    const auto queued_qpc=qpc_now();uint64_t entered_qpc=0,finished_qpc=0,reply_qpc=0;
                    const auto queued=GetTickCount64();ULONGLONG entered=0,finished=0;
                    const auto turn_deadline=nr::registered_lab::FifoDispatch::Clock::now()+std::chrono::milliseconds(stream::remaining(request_deadline));
                    phase="provider";
                    const auto reply=dispatch.execute(ticket,turn_deadline,[&]{
                        entered_qpc=qpc_now();entered=GetTickCount64();recording.request(ticket,bytes);
                        const auto counts=pipeline.retained_pipeline_counts();std::printf("{\"pipeline_before_request\":true,\"dispatch_ticket\":%u,\"uniform\":%zu,\"triangle\":%zu,\"clear\":%zu,\"point\":%zu,\"linear\":%zu}\n",ticket,counts[0],counts[1],counts[2],counts[3],counts[4]);std::fflush(stdout);
                        const auto request=ex::decode(bytes);
                        const auto pixel_digest=shader_digest(request.pixel.bytecode);
                        std::printf("{\"kind\":\"request_identity\",\"pid\":%lu,\"dispatch_ticket\":%u,\"client_pid\":%u,\"sequence\":%u,\"abi\":%u,\"flags\":%u,\"width\":%u,\"height\":%u,\"vertices\":%u,\"pixel_sha256\":\"%s\",\"provider_parent\":%s}\n",GetCurrentProcessId(),ticket,h.client_pid,h.sequence,h.abi,h.flags,h.width,h.height,h.vertices,pixel_digest.c_str(),provider.enabled()?"true":"false");std::fflush(stdout);
                        nr::provider::profile::reset();const auto began=std::chrono::steady_clock::now();const auto before=nr::provider::compile_statistics();const auto coverage_before=nr::raster::coverage_statistics();
                        std::vector<uint8_t> provider_packet;
                        const auto rendered=[&]{try{if(provider.enabled()){auto transported=provider.execute_packet(bytes,h,request_deadline,stop_event.value);provider_packet=std::move(transported.packet);return std::move(transported.metadata);}nr::registered_lab::provider_test_exception(ticket);return pipeline.execute(request);}catch(const std::exception &error){capture_failed_request(bytes,h,ticket);std::fprintf(stderr,"%s\n",error.what());std::fflush(stderr);throw;}catch(...){capture_failed_request(bytes,h,ticket);throw;}}();const auto after=nr::provider::compile_statistics();const auto coverage_after=nr::raster::coverage_statistics();const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
                        ex::require(rendered.execution_device=="NPU");if(!provider.enabled())nr::provider::profile::emit(h.sequence,ticket,rendered.input_bytes,rendered.output_bytes,rendered.timings.shader_ms,rendered.timings.projection_ms,rendered.timings.coverage_ms,rendered.timings.merger_ms,rendered.timings.total_ms);const auto result=provider.enabled()?std::move(provider_packet):ex::response(h,rendered);recording.response(ticket,result);++succeeded;
                        std::printf("{\"client_pid\":%u,\"sequence\":%u,\"execution_device\":\"NPU\",\"server_thread_id\":%lu,\"provider_requests\":%llu,\"draw_ms\":%.3f,\"compile_and_draw_ms\":%.3f,\"compile_calls\":%llu,\"backend_compile_calls\":%llu,\"memory_cache_requests\":%llu,\"memory_cache_hits\":%llu,\"memory_cache_evictions\":%llu,\"cache_requests\":%llu,\"cache_hits\":%llu,\"provider_setup_ms\":%.3f,\"backend_compile_ms\":%.3f,\"compact_coverage_calls\":%llu,\"compact_coverage_samples\":%llu,\"wide_coverage_calls\":%llu,\"passed\":true,\"displayed_frames\":0}\n",
                            h.client_pid,h.sequence,GetCurrentThreadId(),static_cast<unsigned long long>(rendered.provider_requests),rendered.timings.total_ms,elapsed,
                            static_cast<unsigned long long>(after.calls-before.calls),static_cast<unsigned long long>(after.backend_calls-before.backend_calls),static_cast<unsigned long long>(after.memory_requests-before.memory_requests),static_cast<unsigned long long>(after.memory_hits-before.memory_hits),static_cast<unsigned long long>(after.memory_evictions-before.memory_evictions),static_cast<unsigned long long>(after.cache_requests-before.cache_requests),static_cast<unsigned long long>(after.cache_hits-before.cache_hits),double(after.setup_ns-before.setup_ns)/1e6,double(after.backend_ns-before.backend_ns)/1e6,static_cast<unsigned long long>(coverage_after.compact_calls-coverage_before.compact_calls),static_cast<unsigned long long>(coverage_after.compact_samples-coverage_before.compact_samples),static_cast<unsigned long long>(coverage_after.wide_calls-coverage_before.wide_calls));std::fflush(stdout);
                        finished=GetTickCount64();finished_qpc=qpc_now();return result;
                    });
                    dispatched=true;const auto response=ex::Reader(reply).value<ex::Header>();
                    // The complete worker reply has been drained and the FIFO turn
                    // released. A departing application cannot desynchronize or
                    // cancel the provider stream used by the remaining clients.
                    phase="reply";stream::write_packet(pipe,reply,request_deadline,stop_event.value);
                    reply_qpc=qpc_now();phase="ack";uint32_t acknowledged=0;
                    stream::transfer(pipe,&acknowledged,4,false,request_deadline,stop_event.value);ex::require(acknowledged==h.sequence);
                    const auto acknowledged_qpc=qpc_now();
                    std::printf("{\"kind\":\"request_timeline\",\"pid\":%lu,\"dispatch_ticket\":%u,\"client_pid\":%u,\"sequence\":%u,\"provider_parent\":%s,\"qpc_frequency\":%llu,\"connected_qpc\":%llu,\"received_qpc\":%llu,\"queued_qpc\":%llu,\"entered_qpc\":%llu,\"finished_qpc\":%llu,\"reply_qpc\":%llu,\"acknowledged_qpc\":%llu}\n",GetCurrentProcessId(),ticket,h.client_pid,h.sequence,provider.enabled()?"true":"false",static_cast<unsigned long long>(frequency),static_cast<unsigned long long>(connected_qpc),static_cast<unsigned long long>(received_qpc),static_cast<unsigned long long>(queued_qpc),static_cast<unsigned long long>(entered_qpc),static_cast<unsigned long long>(finished_qpc),static_cast<unsigned long long>(reply_qpc),static_cast<unsigned long long>(acknowledged_qpc));std::fflush(stdout);

                    std::printf("{\"sequence\":%u,\"client_pid\":%u,\"dispatch_ticket\":%u,\"request_bytes\":%zu,\"reply_bytes\":%zu,\"queue_wait_ms\":%llu,\"worker_ms\":%llu,\"roundtrip_ms\":%llu,\"worker_success\":%s,\"displayed_frames\":0}\n",
                        h.sequence,h.client_pid,ticket,bytes.size(),reply.size(),static_cast<unsigned long long>(entered-queued),static_cast<unsigned long long>(finished-entered),
                        static_cast<unsigned long long>(GetTickCount64()-started),response.kind==2?"true":"false");std::fflush(stdout);
                } catch(const stream::PipeError &error){
                    if(stopping.load())return;
                    if(!error.client_departure())throw;
                    departure(error.code);
                } catch(const std::invalid_argument &){
                    // Bad local packet/ACK affects this client only. An invalid
                    // worker reply remains fatal because stream integrity is lost.
                    if(std::strcmp(phase,"request") && std::strcmp(phase,"ack"))throw;
                    departure(ERROR_INVALID_DATA);
                }
                if(!DisconnectNamedPipe(pipe) && GetLastError()!=ERROR_PIPE_NOT_CONNECTED)
                    throw std::runtime_error("relay pipe disconnect failed");
                // Count consumed provider turns, including undeliverable replies,
                // so abandoned clients cannot keep an exhausted relay alive.
                if(dispatched && settled_count.fetch_add(1)+1==limit){stop();return;}
            }
        }catch(...){if(!stopping.load())fail(std::current_exception());DisconnectNamedPipe(pipe);}
    };
    if(compositor){std::printf("{\"compositor_peer_prepared\":true,\"pid\":%lu,\"session\":%lu,\"creation_filetime\":%llu,\"request_identity_pinned\":true,\"desktop_admission_performed\":false}\n",compositor->pid(),compositor->session_id(),static_cast<unsigned long long>(compositor->creation_time()));std::fflush(stdout);}
    if(indirect){std::printf("{\"indirect_peer_prepared\":true,\"pid\":%lu,\"session\":%lu,\"creation_filetime\":%llu,\"request_identity_pinned\":true,\"actual_host_execution\":false}\n",indirect->pid(),indirect->session_id(),static_cast<unsigned long long>(indirect->creation_time()));std::fflush(stdout);}
    std::vector<std::thread> threads;
    try{for(const auto &pipe:pipes)threads.emplace_back(serve,pipe->value);}
    catch(...){fail(std::current_exception());}
    std::printf("{\"local_worker_ready\":true,\"pid\":%lu,\"concurrent_clients\":%lu,\"provider_concurrency\":1,\"client_pid_verified\":true,\"private_user_acl\":true,\"network_listener\":false}\n",GetCurrentProcessId(),clients);std::fflush(stdout);
    for(auto &thread:threads)thread.join();
    if(failure){nr::provider::poison_compiled_memory_cache();std::rethrow_exception(failure);}
    std::printf("{\"local_worker_finished\":true,\"succeeded\":%u,\"settled\":%u,\"desktop_offload_tested\":false}\n",succeeded.load(),settled_count.load());
    return succeeded.load()==limit && settled_count.load()==limit?0:1;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
