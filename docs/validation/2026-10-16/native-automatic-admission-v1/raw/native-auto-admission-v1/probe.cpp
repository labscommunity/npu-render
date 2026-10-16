// Finite on-demand admission component. No graphics calls or driver changes.
#include "admission.hpp"
#include <sddl.h>
#include <bcrypt.h>
#include <cstdio>
#include <thread>
namespace a=nr::auto_admission;
namespace n=nr::native_probe;
using a::require;
struct Descriptor {
 PSECURITY_DESCRIPTOR value=nullptr;
 explicit Descriptor(const std::wstring &s){require(ConvertStringSecurityDescriptorToSecurityDescriptorW(s.c_str(),SDDL_REVISION_1,&value,nullptr)!=FALSE);}
 ~Descriptor(){if(value)LocalFree(value);}
};
std::wstring sid_text(){const auto sid=n::user_sid(GetCurrentProcess());LPWSTR text=nullptr;require(ConvertSidToStringSidW(const_cast<uint8_t*>(sid.data()),&text)!=FALSE);std::wstring result(text);LocalFree(text);return result;}
struct Mapping {
 HANDLE value=nullptr;
 Mapping(const std::wstring &name,const void *data,DWORD bytes){
  Descriptor sd(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;"+sid_text()+L")");SECURITY_ATTRIBUTES sa{sizeof(sa),sd.value,FALSE};
  try{value=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,bytes,name.c_str());const auto error=GetLastError();require(value&&error!=ERROR_ALREADY_EXISTS);
   const auto view=MapViewOfFile(value,FILE_MAP_WRITE,0,0,bytes);require(view!=nullptr);std::memcpy(view,data,bytes);require(UnmapViewOfFile(view)!=FALSE);
  }catch(...){if(value)CloseHandle(value);value=nullptr;throw;}
 }
 ~Mapping(){if(value)CloseHandle(value);}
 Mapping(const Mapping &)=delete;
};
struct Child {
 HANDLE process=nullptr;DWORD pid=0;
 explicit Child(const std::wstring &args){
  wchar_t exe[32768]{};const auto length=GetModuleFileNameW(nullptr,exe,32768);require(length&&length<32768);
  auto command=L"\""+std::wstring(exe)+L"\" "+args;STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION info{};
  require(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&info)!=FALSE);process=info.hProcess;pid=info.dwProcessId;CloseHandle(info.hThread);
 }
 DWORD wait(){require(WaitForSingleObject(process,8000)==WAIT_OBJECT_0);DWORD code=0;require(GetExitCodeProcess(process,&code)!=FALSE);return code;}
 ~Child(){if(process){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,1);WaitForSingleObject(process,5000);}CloseHandle(process);}}
};
int consumer(unsigned mutation,bool absent){
 bool preexisting=false;try{(void)n::read_mapping();preexisting=true;}catch(...){}require(!preexisting);
 bool admitted=false;
 try{const auto lease=a::request(mutation);n::Admission proof(lease);proof.verify();admitted=true;}catch(...){if(!mutation&&!absent)throw;}
 require(admitted==(!mutation&&!absent));
 if(!admitted){bool mapping=false;try{(void)n::read_mapping();mapping=true;}catch(...){}require(!mapping);}
 std::printf("{\"consumer_passed\":true,\"pid\":%lu,\"admitted\":%s,\"started_suspended\":false,\"preexisting_lease\":false,\"mutation\":%u,\"publication_absent\":%s,\"npu_tested\":false}\n",GetCurrentProcessId(),admitted?"true":"false",mutation,absent?"true":"false");return 0;
}
int wmain(int argc,wchar_t **argv)try{
 if(argc==2&&!std::wcscmp(argv[1],L"--idle")){Sleep(120000);return 0;}
 if(argc==3&&!std::wcscmp(argv[1],L"--consumer")){const auto mutation=unsigned(std::wcstoul(argv[2],nullptr,10));require(mutation<=5);return consumer(mutation,false);}
 if(argc==2&&!std::wcscmp(argv[1],L"--absent"))return consumer(0,true);
 require(argc==1);Child worker(L"--idle");a::Publication p{};p.controller=a::identity(GetCurrentProcess());p.worker=a::identity(worker.process);p.session=p.controller.session;p.issued=GetTickCount64();p.deadline=p.issued+60000;
 const auto sid=n::user_sid(GetCurrentProcess());p.sid_bytes=DWORD(sid.size());std::memcpy(p.sid,sid.data(),sid.size());
 require(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(p.nonce),sizeof(p.nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0);p.nonce[0]|=1;
 unsigned char random[16]{};require(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0);const char digits[]="0123456789abcdef";for(unsigned i=0;i<16;++i){p.pipe_suffix[i*2]=digits[random[i]>>4];p.pipe_suffix[i*2+1]=digits[random[i]&15];}
 unsigned accepted=0,rejected=0;
 {
  Mapping publication(a::publication_name(p.session),&p,sizeof(p));
  Descriptor sd(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100003;;;"+sid_text()+L")");SECURITY_ATTRIBUTES sa{sizeof(sa),sd.value,FALSE};
  a::Handle pipe(CreateNamedPipeW(a::pipe_name(p).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_REJECT_REMOTE_CLIENTS,1,256,256,0,&sa));
  // Both positive consumers start normally and have unknown PIDs until connection.
  for(unsigned mutation:{1u,2u,3u,4u,0u,0u,5u}){
   a::Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED op{};op.hEvent=event.value;const auto ready=ConnectNamedPipe(pipe.value,&op);const auto error=ready?ERROR_SUCCESS:GetLastError();
   Child child(L"--consumer "+std::to_wstring(mutation));const auto deadline=GetTickCount64()+5000;
   if(error!=ERROR_PIPE_CONNECTED)(void)a::complete(pipe.value,op,ready,error,0,deadline);
   ULONG actual=0;require(GetNamedPipeClientProcessId(pipe.value,&actual)!=FALSE);
   a::Request request{};a::transfer(pipe.value,&request,sizeof(request),false,deadline);a::Reply answer{};std::unique_ptr<Mapping> lease_mapping;
   if(mutation==5){const auto began=GetTickCount64();require(child.wait()==0);const auto elapsed=GetTickCount64()-began;require(elapsed>=2000&&elapsed<=5000);require(DisconnectNamedPipe(pipe.value)!=FALSE);std::printf("{\"unresponsive_broker_rejected\":true,\"wait_ms\":%llu}\n",static_cast<unsigned long long>(elapsed));continue;}
   a::Handle client(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,actual));const auto peer=a::identity(client.value);const auto peer_sid=n::user_sid(client.value);
   const bool allowed=request.signature==a::magic&&request.version==1&&request.bytes==sizeof(request)&&a::same(request.consumer,peer)&&peer.session==p.session&&peer_sid==sid&&!std::memcmp(request.nonce,p.nonce,sizeof(p.nonce))&&WaitForSingleObject(client.value,0)==WAIT_TIMEOUT;
   if(allowed){
    n::Lease lease{};lease.controller=p.controller;lease.worker=p.worker;lease.consumer=peer;lease.issued_tick=GetTickCount64();lease.deadline_tick=lease.issued_tick+30000;lease.nonce[0]=p.nonce[0];lease.nonce[1]=p.nonce[1];std::memcpy(lease.pipe_suffix,p.pipe_suffix,32);n::set_consumer_sid(lease,client.value);
    require(n::shape_valid(lease,GetTickCount64(),peer.pid,peer.created,peer.session));lease_mapping=std::make_unique<Mapping>(n::mapping_name(peer.pid),&lease,sizeof(lease));answer.accepted=1;++accepted;
   }else ++rejected;
   a::transfer(pipe.value,&answer,sizeof(answer),true,deadline);require(child.wait()==0);require(DisconnectNamedPipe(pipe.value)!=FALSE);lease_mapping.reset();
   const auto absent=OpenFileMappingW(FILE_MAP_READ,FALSE,n::mapping_name(peer.pid).c_str());if(absent)CloseHandle(absent);require(!absent);
   std::printf("{\"broker_case_passed\":true,\"actual_client_pid\":%lu,\"accepted\":%s,\"mutation\":%u,\"lease_removed\":true}\n",actual,answer.accepted?"true":"false",mutation);std::fflush(stdout);
  }
 }
 Child absent(L"--absent");require(absent.wait()==0);require(accepted==2&&rejected==4);
 std::printf("{\"completed\":true,\"passed\":true,\"automatic_lease_consumers\":2,\"rejected_spoofs\":4,\"stopped_broker_rejected\":true,\"owned_mappings_removed\":true,\"driver_changed\":false,\"graphics_tested\":false,\"npu_tested\":false}\n");return 0;
}catch(const std::exception &error){std::fprintf(stderr,"automatic admission component: %s (win32 %lu)\n",error.what(),GetLastError());return 1;}
