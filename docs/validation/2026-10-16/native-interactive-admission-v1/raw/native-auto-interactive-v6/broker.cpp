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

int wmain(int argc,wchar_t **argv)try{
 require(argc==3||argc==4);wchar_t *end=nullptr;const auto pid=std::wcstoul(argv[1],&end,10);require(end!=argv[1]&&!*end&&pid);
 const std::wstring suffix=argv[2];require(suffix.size()==32);for(auto c:suffix)require((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f'));
 a::Handle worker(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
 a::Publication p{};p.controller=a::identity(GetCurrentProcess());p.worker=a::identity(worker.value);p.session=p.controller.session;
 if(argc==4){wchar_t *session_end=nullptr;const auto target=std::wcstoul(argv[3],&session_end,10);require(session_end!=argv[3]&&!*session_end&&target&&target!=UINT32_MAX&&p.controller.session==0&&p.worker.session==0&&n::session_unlocked(target));p.session=target;p.scope=1;}
 p.issued=GetTickCount64();p.deadline=p.issued+120000;
 const auto sid=n::user_sid(GetCurrentProcess());require(n::user_sid(worker.value)==sid&&p.worker.session==p.controller.session&&p.worker.pid!=p.controller.pid);a::verify_peer(p.worker);
 p.sid_bytes=DWORD(sid.size());std::memcpy(p.sid,sid.data(),sid.size());for(size_t i=0;i<32;++i)p.pipe_suffix[i]=char(suffix[i]);
 require(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(p.nonce),sizeof(p.nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0);p.nonce[0]|=1;
 DWORD actual=0;
 {
  Mapping publication(a::publication_name(p.session),&p,DWORD(sizeof(p)));
  Descriptor sd(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100003;;;"+sid_text()+L")S:(ML;;NW;;;ME)");SECURITY_ATTRIBUTES sa{sizeof(sa),sd.value,FALSE};
  a::Handle pipe(CreateNamedPipeW(a::pipe_name(p).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_REJECT_REMOTE_CLIENTS,1,256,256,0,&sa));
  PSECURITY_DESCRIPTOR pipe_security=nullptr;const auto security_flags=OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|LABEL_SECURITY_INFORMATION;
  require(GetSecurityInfo(pipe.value,SE_KERNEL_OBJECT,security_flags,nullptr,nullptr,nullptr,nullptr,&pipe_security)==ERROR_SUCCESS);LPWSTR description=nullptr;
  require(ConvertSecurityDescriptorToStringSecurityDescriptorW(pipe_security,SDDL_REVISION_1,security_flags,&description,nullptr)!=FALSE);
  std::printf("{\"pipe_security_verified\":true,\"sddl\":\"%ls\"}\n",description);LocalFree(description);LocalFree(pipe_security);
  std::printf("{\"automatic_broker_ready\":true,\"pid\":%lu,\"worker_pid\":%lu,\"session\":%lu,\"consumer_started_by_broker\":false}\n",GetCurrentProcessId(),pid,DWORD(p.session));std::fflush(stdout);
  a::Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED op{};op.hEvent=event.value;const auto ready=ConnectNamedPipe(pipe.value,&op);const auto error=ready?ERROR_SUCCESS:GetLastError();
  const auto deadline=GetTickCount64()+30000;if(error!=ERROR_PIPE_CONNECTED)(void)a::complete(pipe.value,op,ready,error,0,deadline);
  require(GetNamedPipeClientProcessId(pipe.value,&actual)!=FALSE);a::Handle client(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,actual));
  const auto peer=a::identity(client.value);const auto peer_sid=n::user_sid(client.value);
  a::Request request{};a::transfer(pipe.value,&request,sizeof(request),false,deadline);
  std::printf("{\"automatic_request_received\":true,\"actual_client_pid\":%lu,\"actual_session\":%lu,\"claimed_client_pid\":%lu,\"claimed_session\":%lu,\"creation_matches\":%s,\"sid_matches\":%s,\"signature_matches\":%s,\"nonce_matches\":%s}\n",actual,DWORD(peer.session),DWORD(request.consumer.pid),DWORD(request.consumer.session),request.consumer.created==peer.created?"true":"false",peer_sid==sid?"true":"false",request.signature==a::magic?"true":"false",!std::memcmp(request.nonce,p.nonce,sizeof(p.nonce))?"true":"false");std::fflush(stdout);
  require(request.signature==a::magic&&request.version==2&&request.bytes==sizeof(request)&&a::same(request.consumer,peer)&&peer.session==p.session&&peer_sid==sid&&!std::memcmp(request.nonce,p.nonce,sizeof(p.nonce))&&WaitForSingleObject(client.value,0)==WAIT_TIMEOUT);
  n::Lease lease{};if(p.scope){lease.scope=3;lease.desktop_session=p.session;}lease.controller=p.controller;lease.worker=p.worker;lease.consumer=peer;lease.issued_tick=GetTickCount64();lease.deadline_tick=p.deadline;std::memcpy(lease.nonce,p.nonce,sizeof(p.nonce));std::memcpy(lease.pipe_suffix,p.pipe_suffix,32);n::set_consumer_sid(lease,client.value);
  require(n::shape_valid(lease,GetTickCount64(),peer.pid,peer.created,peer.session));
  {
   Mapping lease_mapping(n::mapping_name(peer.pid),&lease,DWORD(sizeof(lease)));a::Reply answer{};answer.accepted=1;a::transfer(pipe.value,&answer,sizeof(answer),true,deadline);
   std::printf("{\"automatic_consumer_admitted\":true,\"client_pid\":%lu,\"session\":%lu,\"worker_pid\":%lu,\"actual_pipe_client_verified\":true}\n",actual,DWORD(peer.session),pid);std::fflush(stdout);
   require(WaitForSingleObject(client.value,110000)==WAIT_OBJECT_0);require(DisconnectNamedPipe(pipe.value)!=FALSE);
  }
  const auto absent=OpenFileMappingW(FILE_MAP_READ,FALSE,n::mapping_name(peer.pid).c_str());if(absent)CloseHandle(absent);require(!absent);
 }
 const auto absent=OpenFileMappingW(FILE_MAP_READ,FALSE,a::publication_name(p.session).c_str());if(absent)CloseHandle(absent);require(!absent);
 std::printf("{\"automatic_broker_completed\":true,\"passed\":true,\"client_pid\":%lu,\"owned_mappings_removed\":true,\"npu_tested_by_broker\":false}\n",actual);return 0;
}catch(const std::exception &error){std::fprintf(stderr,"automatic broker: %s (win32 %lu)\n",error.what(),GetLastError());return 1;}
