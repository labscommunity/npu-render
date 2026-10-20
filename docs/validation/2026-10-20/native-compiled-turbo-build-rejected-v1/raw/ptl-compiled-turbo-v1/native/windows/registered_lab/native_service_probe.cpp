// Owned LocalService account diagnostic; not WUDFHost or DWM execution.
#define main native_component_main
#include "native_probe_ddi_test.cpp"
#undef main
#include <thread>
#include <sddl.h>
#include <io.h>
#include <fcntl.h>
void redirect_shared(FILE *stream,const std::wstring &path){
    FILE *opened=nullptr;nr::native_probe::require(_wfreopen_s(&opened,L"NUL",L"wb",stream)==0);
    const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);nr::native_probe::require(handle!=INVALID_HANDLE_VALUE);
    const auto fd=_open_osfhandle(reinterpret_cast<intptr_t>(handle),_O_WRONLY|_O_BINARY);if(fd<0){CloseHandle(handle);throw std::runtime_error("output handle conversion failed");}
    const auto code=_dup2(fd,_fileno(stream));_close(fd);nr::native_probe::require(code==0);
}
int wmain(int argc,wchar_t **argv) try {
    if(argc!=2)return 2;
    DWORD session=UINT32_MAX;nr::native_probe::require(ProcessIdToSessionId(GetCurrentProcessId(),&session) && session==0);
    const auto sid=nr::native_probe::user_sid(GetCurrentProcess());nr::native_probe::require(IsWellKnownSid(const_cast<uint8_t*>(sid.data()),WinLocalServiceSid)!=FALSE);
    const auto directory=std::wstring(argv[1]);
    redirect_shared(stdout,directory+LR"(\service.stdout.jsonl)");redirect_shared(stderr,directory+LR"(\service.stderr.log)");
    std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
    std::thread([]{Sleep(110000);ExitProcess(124);}).detach();
    char executable[]="native_service_probe",negative[]="--reject-unleased";char *args[]={executable,negative};
    const auto rejected=native_component_main(2,args);if(rejected)return rejected;
    std::printf("{\"local_service_probe_ready\":true,\"pid\":%lu,\"session\":%lu,\"local_service_identity_verified\":true,\"creation_filetime\":%llu,\"dwm_execution\":false,\"umdf_execution\":false}\n",GetCurrentProcessId(),session,static_cast<unsigned long long>(nr::native_probe::creation_time(GetCurrentProcess())));
    const auto deadline=GetTickCount64()+30000;
    const auto ready_name=nr::native_probe::mapping_name(GetCurrentProcessId())+L".Ready";
    while(GetTickCount64()<deadline){HANDLE ready=OpenEventW(SYNCHRONIZE,FALSE,ready_name.c_str());if(ready){const auto result=WaitForSingleObject(ready,DWORD(deadline-GetTickCount64()));CloseHandle(ready);if(result==WAIT_OBJECT_0)return native_component_main(1,args);return 6;}const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND)return 4;Sleep(20);}
    return 5;
}catch(const std::exception &e){std::fprintf(stderr,"LocalService probe: %s (%lu)\n",e.what(),GetLastError());return 1;}
