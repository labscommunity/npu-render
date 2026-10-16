#include "admission.hpp"
#include <cstdio>
int wmain(int argc,wchar_t **argv)try{
 using nr::native_probe::require;
 if(argc==2&&!std::wcscmp(argv[1],L"--idle")){Sleep(180000);return 0;}
 require(argc==3&&!std::wcscmp(argv[1],L"--consumer"));wchar_t *end=nullptr;const auto mutation=unsigned(std::wcstoul(argv[2],&end,10));require(end!=argv[2]&&!*end&&mutation<=6&&mutation!=5);
 const auto identity=nr::auto_admission::identity(GetCurrentProcess());const auto name=nr::native_probe::mapping_name(identity.pid);const auto existing=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(existing)CloseHandle(existing);require(!existing);
 bool admitted=false;unsigned scope=99;
 try{const auto lease=nr::auto_admission::request(mutation);nr::native_probe::Admission proof(lease);proof.verify();admitted=true;scope=lease.scope;}catch(...){if(!mutation)throw;}
 require(admitted==!mutation);
 if(!admitted){const auto leaked=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(leaked)CloseHandle(leaked);require(!leaked);}
 std::printf("{\"consumer_passed\":true,\"pid\":%u,\"session\":%u,\"admitted\":%s,\"scope\":%u,\"mutation\":%u,\"preexisting_lease\":false,\"started_suspended\":false,\"npu_tested\":false}\n",identity.pid,identity.session,admitted?"true":"false",scope,mutation);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"interactive consumer: %s at %s (win32 %lu)\n",e.what(),nr::auto_admission::diagnostic_stage,GetLastError());return 1;}
