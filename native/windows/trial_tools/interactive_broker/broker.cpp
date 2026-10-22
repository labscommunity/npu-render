// Finite on-demand admission component. No graphics calls or driver changes.
// One-shot mode (3 or 4 arguments) publishes for one client and ends with it. Serve mode
// (`<worker pid> <suffix> <session> --serve <stop file>`) keeps the session's publication
// current and admits every same-user interactive client that asks, renewing each admitted
// lease in place (scope 3 grants are at most 120 s) until the client exits, the stop file
// appears or the worker ends. Every admission, rejection and renewal round is one JSON row.
#include "admission.hpp"
#include <sddl.h>
#include <bcrypt.h>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>
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
 HANDLE value=nullptr;DWORD bytes=0;
 Mapping(const std::wstring &name,const void *data,DWORD size):bytes(size){
  Descriptor sd(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;"+sid_text()+L")");SECURITY_ATTRIBUTES sa{sizeof(sa),sd.value,FALSE};
  try{value=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,bytes,name.c_str());const auto error=GetLastError();require(value&&error!=ERROR_ALREADY_EXISTS);write(data);}
  catch(...){if(value)CloseHandle(value);value=nullptr;throw;}
 }
 // In-place rewrite: a lease renewal changes only its ticks, a publication refresh only its window.
 void write(const void *data){const auto view=MapViewOfFile(value,FILE_MAP_WRITE,0,0,bytes);require(view!=nullptr);std::memcpy(view,data,bytes);require(UnmapViewOfFile(view)!=FALSE);}
 ~Mapping(){if(value)CloseHandle(value);}
 Mapping(const Mapping &)=delete;
};
struct Client {std::unique_ptr<a::Handle> process;a::ProcessIdentity identity{};std::unique_ptr<Mapping> mapping;n::Lease lease{};unsigned renewals=0;};
constexpr uint64_t grant_ms=120000,renew_every_ms=40000;

int wmain(int argc,wchar_t **argv)try{
 const bool serve=argc==6&&!std::wcscmp(argv[4],L"--serve");
 require(argc==3||argc==4||serve);wchar_t *end=nullptr;const auto pid=std::wcstoul(argv[1],&end,10);require(end!=argv[1]&&!*end&&pid);
 const std::wstring suffix=argv[2];require(suffix.size()==32);for(auto c:suffix)require((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f'));
 a::Handle worker(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
 a::Publication p{};p.controller=a::identity(GetCurrentProcess());p.worker=a::identity(worker.value);p.session=p.controller.session;
 if(argc>=4){wchar_t *session_end=nullptr;const auto target=std::wcstoul(argv[3],&session_end,10);require(session_end!=argv[3]&&!*session_end&&target&&target!=UINT32_MAX&&p.controller.session==0&&p.worker.session==0&&n::session_unlocked(target));p.session=target;p.scope=1;}
 p.issued=GetTickCount64();p.deadline=p.issued+grant_ms;
 const auto sid=n::user_sid(GetCurrentProcess());require(n::user_sid(worker.value)==sid&&p.worker.session==p.controller.session&&p.worker.pid!=p.controller.pid);a::verify_peer(p.worker);
 p.sid_bytes=DWORD(sid.size());std::memcpy(p.sid,sid.data(),sid.size());for(size_t i=0;i<32;++i)p.pipe_suffix[i]=char(suffix[i]);
 require(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(p.nonce),sizeof(p.nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0);p.nonce[0]|=1;
 DWORD actual=0;unsigned admitted_total=0,rejected_total=0,renewal_rounds=0;const char *reason="one-shot";
 {
  Mapping publication(a::publication_name(p.session),&p,DWORD(sizeof(p)));
  Descriptor sd(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100083;;;"+sid_text()+L")S:(ML;;NW;;;ME)");SECURITY_ATTRIBUTES sa{sizeof(sa),sd.value,FALSE};
  a::Handle pipe(CreateNamedPipeW(a::pipe_name(p).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_REJECT_REMOTE_CLIENTS,1,256,256,0,&sa));
  PSECURITY_DESCRIPTOR pipe_security=nullptr;const auto security_flags=OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|LABEL_SECURITY_INFORMATION;
  require(GetSecurityInfo(pipe.value,SE_KERNEL_OBJECT,security_flags,nullptr,nullptr,nullptr,nullptr,&pipe_security)==ERROR_SUCCESS);LPWSTR description=nullptr;
  require(ConvertSecurityDescriptorToStringSecurityDescriptorW(pipe_security,SDDL_REVISION_1,security_flags,&description,nullptr)!=FALSE);
  std::printf("{\"pipe_security_verified\":true,\"sddl\":\"%ls\"}\n",description);LocalFree(description);LocalFree(pipe_security);
  std::printf("{\"automatic_broker_ready\":true,\"pid\":%lu,\"worker_pid\":%lu,\"session\":%lu,\"consumer_started_by_broker\":false,\"serve\":%s,\"grant_ms\":%llu}\n",GetCurrentProcessId(),pid,DWORD(p.session),serve?"true":"false",static_cast<unsigned long long>(grant_ms));std::fflush(stdout);
  // Waits up to wait_ms for one client connection; false on timeout (the pending connect is cancelled).
  const auto accept=[&](DWORD wait_ms){
   a::Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED op{};op.hEvent=event.value;const auto ready=ConnectNamedPipe(pipe.value,&op);const auto error=ready?ERROR_SUCCESS:GetLastError();
   if(error==ERROR_PIPE_CONNECTED)return true;require(error==ERROR_IO_PENDING);
   if(WaitForSingleObject(event.value,wait_ms)==WAIT_OBJECT_0){DWORD bytes=0;require(GetOverlappedResult(pipe.value,&op,&bytes,FALSE)!=FALSE);return true;}
   CancelIoEx(pipe.value,&op);DWORD ignored=0;GetOverlappedResult(pipe.value,&op,&ignored,TRUE);return false;
  };
  // Verifies the connected client against the publication and grants it a lease mapping.
  const auto admit=[&](){
   const auto deadline=GetTickCount64()+30000;
   require(GetNamedPipeClientProcessId(pipe.value,&actual)!=FALSE);auto client=std::make_unique<a::Handle>(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,actual));
   const auto peer=a::identity(client->value);const auto peer_sid=n::user_sid(client->value);
   a::Request request{};a::transfer(pipe.value,&request,sizeof(request),false,deadline);
   std::printf("{\"automatic_request_received\":true,\"actual_client_pid\":%lu,\"actual_session\":%lu,\"claimed_client_pid\":%lu,\"claimed_session\":%lu,\"creation_matches\":%s,\"sid_matches\":%s,\"signature_matches\":%s,\"nonce_matches\":%s}\n",actual,DWORD(peer.session),DWORD(request.consumer.pid),DWORD(request.consumer.session),request.consumer.created==peer.created?"true":"false",peer_sid==sid?"true":"false",request.signature==a::magic?"true":"false",!std::memcmp(request.nonce,p.nonce,sizeof(p.nonce))?"true":"false");std::fflush(stdout);
   require(request.signature==a::magic&&request.version==2&&request.bytes==sizeof(request)&&a::same(request.consumer,peer)&&peer.session==p.session&&peer_sid==sid&&!std::memcmp(request.nonce,p.nonce,sizeof(p.nonce))&&WaitForSingleObject(client->value,0)==WAIT_TIMEOUT);
   Client c;c.lease=n::Lease{};if(p.scope){c.lease.scope=3;c.lease.desktop_session=p.session;}c.lease.controller=p.controller;c.lease.worker=p.worker;c.lease.consumer=peer;c.lease.issued_tick=GetTickCount64();c.lease.deadline_tick=serve?c.lease.issued_tick+grant_ms:p.deadline;std::memcpy(c.lease.nonce,p.nonce,sizeof(p.nonce));std::memcpy(c.lease.pipe_suffix,p.pipe_suffix,32);n::set_consumer_sid(c.lease,client->value);
   require(n::shape_valid(c.lease,GetTickCount64(),peer.pid,peer.created,peer.session));
   c.mapping=std::make_unique<Mapping>(n::mapping_name(peer.pid),&c.lease,DWORD(sizeof(c.lease)));c.identity=peer;c.process=std::move(client);
   a::Reply answer{};answer.accepted=1;a::transfer(pipe.value,&answer,sizeof(answer),true,deadline);++admitted_total;
   std::printf("{\"automatic_consumer_admitted\":true,\"client_pid\":%lu,\"session\":%lu,\"worker_pid\":%lu,\"actual_pipe_client_verified\":true,\"admitted_total\":%u,\"lease_deadline_tick\":%llu}\n",actual,DWORD(peer.session),pid,admitted_total,static_cast<unsigned long long>(c.lease.deadline_tick));std::fflush(stdout);
   return c;
  };
  if(!serve){
   require(accept(30000));Client c=admit();
   require(WaitForSingleObject(c.process->value,110000)==WAIT_OBJECT_0);require(DisconnectNamedPipe(pipe.value)!=FALSE);c.mapping.reset();
   const auto absent=OpenFileMappingW(FILE_MAP_READ,FALSE,n::mapping_name(c.identity.pid).c_str());if(absent)CloseHandle(absent);require(!absent);
  }else{
   const std::wstring stop=argv[5];std::vector<Client> clients;uint64_t last_renewal=GetTickCount64();
   while(true){
    if(GetFileAttributesW(stop.c_str())!=INVALID_FILE_ATTRIBUTES){reason="stop-file";break;}
    if(WaitForSingleObject(worker.value,0)!=WAIT_TIMEOUT){reason="worker-exited";break;}
    if(accept(1000)){
     try{clients.push_back(admit());}
     catch(const std::exception &e){++rejected_total;std::printf("{\"automatic_request_rejected\":true,\"client_pid\":%lu,\"reason\":\"%s\",\"win32\":%lu}\n",actual,e.what(),GetLastError());std::fflush(stdout);}
     DisconnectNamedPipe(pipe.value);
    }
    const auto now=GetTickCount64();
    if(now-last_renewal>=renew_every_ms){
     last_renewal=now;p.issued=now;p.deadline=now+grant_ms;publication.write(&p);unsigned live=0,pruned=0;
     for(auto it=clients.begin();it!=clients.end();){
      if(WaitForSingleObject(it->process->value,0)!=WAIT_TIMEOUT){++pruned;it=clients.erase(it);continue;}
      it->lease.issued_tick=now;it->lease.deadline_tick=now+grant_ms;it->mapping->write(&it->lease);++it->renewals;++live;++it;
     }
     ++renewal_rounds;std::printf("{\"automatic_leases_renewed\":true,\"round\":%u,\"live_clients\":%u,\"pruned\":%u,\"publication_deadline_tick\":%llu}\n",renewal_rounds,live,pruned,static_cast<unsigned long long>(p.deadline));std::fflush(stdout);
    }
   }
   clients.clear();
  }
 }
 const auto absent=OpenFileMappingW(FILE_MAP_READ,FALSE,a::publication_name(p.session).c_str());if(absent)CloseHandle(absent);require(!absent);
 std::printf("{\"automatic_broker_completed\":true,\"passed\":true,\"client_pid\":%lu,\"owned_mappings_removed\":true,\"npu_tested_by_broker\":false,\"serve\":%s,\"reason\":\"%s\",\"admitted_total\":%u,\"rejected_total\":%u,\"renewal_rounds\":%u}\n",actual,serve?"true":"false",reason,admitted_total,rejected_total,renewal_rounds);return 0;
}catch(const std::exception &error){std::fprintf(stderr,"automatic broker: %s (win32 %lu)\n",error.what(),GetLastError());return 1;}
