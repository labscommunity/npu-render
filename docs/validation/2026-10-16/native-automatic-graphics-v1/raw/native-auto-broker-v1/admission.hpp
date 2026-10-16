#pragma once
#include "native_probe_lease.hpp"
#include <algorithm>
#include <array>
#include <memory>
namespace nr::auto_admission {
using nr::native_probe::Handle;
using nr::native_probe::ProcessIdentity;
using nr::native_probe::require;
constexpr uint64_t magic=0x314d4441524e3854ull;
struct Publication {
 uint64_t signature=magic;uint32_t version=1,bytes=sizeof(Publication);
 ProcessIdentity controller,worker;
 uint64_t issued=0,deadline=0,nonce[2]{};
 char pipe_suffix[32]{};
 uint32_t session=0,sid_bytes=0;uint8_t sid[68]{};uint32_t reserved=0;
};
struct Request {uint64_t signature=magic;uint32_t version=1,bytes=sizeof(Request);ProcessIdentity consumer;uint64_t nonce[2]{};};
struct Reply {uint64_t signature=magic;uint32_t accepted=0,reserved=0;};
static_assert(sizeof(Publication)==192&&sizeof(Request)==48&&sizeof(Reply)==16);
inline ProcessIdentity identity(HANDLE p){ProcessIdentity i{};DWORD session=0;i.pid=GetProcessId(p);require(i.pid&&ProcessIdToSessionId(i.pid,&session));i.session=session;i.created=nr::native_probe::creation_time(p);return i;}
inline bool same(const ProcessIdentity &a,const ProcessIdentity &b){return a.pid==b.pid&&a.created==b.created&&a.session==b.session;}
inline std::wstring publication_name(DWORD session){return L"Global\\T8.NpuRender.AutoAdmission.v1."+std::to_wstring(session);}
inline std::wstring pipe_name(const Publication &p){return L"\\\\.\\pipe\\T8.NpuRender.AutoAdmission.v1."+std::wstring(p.pipe_suffix,p.pipe_suffix+32);}
inline void verify_peer(const ProcessIdentity &i){Handle p(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,i.pid));require(same(identity(p.value),i)&&WaitForSingleObject(p.value,0)==WAIT_TIMEOUT);}
inline void validate(const Publication &p){
 const auto now=GetTickCount64();const auto own=identity(GetCurrentProcess());
 require(p.signature==magic&&p.version==1&&p.bytes==sizeof(p)&&!p.reserved&&p.issued&&p.issued<=now&&p.deadline>now&&p.deadline>p.issued&&p.deadline-p.issued<=120000&&p.session==own.session&&p.controller.session==p.session&&p.worker.session==p.session&&(p.nonce[0]||p.nonce[1]));
 require(p.controller.pid!=p.worker.pid&&p.controller.pid!=own.pid&&p.worker.pid!=own.pid);
 nr::native_probe::Lease sid{};sid.sid_bytes=p.sid_bytes;std::memcpy(sid.consumer_sid,p.sid,68);require(nr::native_probe::sid_valid(sid));
 const auto user=nr::native_probe::user_sid(GetCurrentProcess());require(user.size()==p.sid_bytes&&!std::memcmp(user.data(),p.sid,user.size()));
 for(const char c:p.pipe_suffix)require((c>='0'&&c<='9')||(c>='a'&&c<='f'));
 verify_peer(p.controller);verify_peer(p.worker);
}
inline Publication discover(){
 const auto name=publication_name(identity(GetCurrentProcess()).session);
 Handle map(OpenFileMappingW(FILE_MAP_READ|READ_CONTROL,FALSE,name.c_str()));PSECURITY_DESCRIPTOR sd=nullptr;
 require(GetSecurityInfo(map.value,SE_KERNEL_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr,&sd)==ERROR_SUCCESS);
 const bool permitted=nr::native_probe::readonly_to_unprivileged(sd);LocalFree(sd);require(permitted);
 const auto view=MapViewOfFile(map.value,FILE_MAP_READ,0,0,sizeof(Publication));require(view!=nullptr);Publication p{};std::memcpy(&p,view,sizeof(p));UnmapViewOfFile(view);validate(p);return p;
}
inline DWORD complete(HANDLE pipe,OVERLAPPED &op,BOOL ready,DWORD error,DWORD immediate,uint64_t deadline){
 if(ready)return immediate;
 require(error==ERROR_IO_PENDING);const auto now=GetTickCount64();const auto wait=WaitForSingleObject(op.hEvent,now>=deadline?0:DWORD((std::min)(deadline-now,uint64_t(5000))));
 if(wait!=WAIT_OBJECT_0){CancelIoEx(pipe,&op);DWORD ignored=0;GetOverlappedResult(pipe,&op,&ignored,TRUE);throw std::runtime_error("admission I/O deadline");}
 DWORD bytes=0;require(GetOverlappedResult(pipe,&op,&bytes,FALSE)!=FALSE);return bytes;
}
inline void transfer(HANDLE pipe,void *buffer,DWORD size,bool write,uint64_t deadline){
 auto *data=static_cast<uint8_t*>(buffer);DWORD cursor=0;
 while(cursor<size){Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED op{};op.hEvent=event.value;DWORD immediate=0;
  const auto ready=write?WriteFile(pipe,data+cursor,size-cursor,&immediate,&op):ReadFile(pipe,data+cursor,size-cursor,&immediate,&op);const auto error=ready?ERROR_SUCCESS:GetLastError();
  const auto count=complete(pipe,op,ready,error,immediate,deadline);require(count&&count<=size-cursor);cursor+=count;
 }
}
inline nr::native_probe::Lease request(unsigned mutation=0){
 const auto p=discover();const auto name=pipe_name(p);const auto deadline=GetTickCount64()+3000;
 Handle pipe(CreateFileW(name.c_str(),FILE_READ_DATA|FILE_WRITE_DATA|SYNCHRONIZE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
 ULONG server=0;require(GetNamedPipeServerProcessId(pipe.value,&server)&&server==p.controller.pid);verify_peer(p.controller);
 Request q{};q.consumer=identity(GetCurrentProcess());std::memcpy(q.nonce,p.nonce,sizeof(q.nonce));
 if(mutation==1)q.consumer.pid=p.worker.pid;
 if(mutation==2)++q.consumer.created;
 if(mutation==3)q.signature^=1;
 if(mutation==4)q.nonce[0]^=1;
 transfer(pipe.value,&q,sizeof(q),true,deadline);Reply answer{};transfer(pipe.value,&answer,sizeof(answer),false,deadline);
 require(answer.signature==magic&&answer.accepted==1&&!answer.reserved);
 auto lease=nr::native_probe::read_mapping();require(same(lease.controller,p.controller)&&same(lease.worker,p.worker));nr::native_probe::Admission proof(lease);proof.verify();return lease;
}
}
