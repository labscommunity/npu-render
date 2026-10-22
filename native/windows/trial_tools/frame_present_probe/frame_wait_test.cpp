#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "frame_wait.hpp"
#include <cstdio>
#include <thread>
namespace fs=std::filesystem;
void require(bool value){if(!value)throw std::runtime_error("frame wait contract failed");}
void messages(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){};}
void write(const fs::path &path,DWORD value){
    const auto f=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(f!=INVALID_HANDLE_VALUE);DWORD n=0;const auto ok=WriteFile(f,&value,sizeof(value),&n,nullptr);const auto closed=CloseHandle(f);require(ok && n==sizeof(value) && closed);
}
DWORD read(const fs::path &path){
    const auto f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(f!=INVALID_HANDLE_VALUE);DWORD n=0,value=0;const auto ok=ReadFile(f,&value,sizeof(value),&n,nullptr);const auto closed=CloseHandle(f);require(ok && n==sizeof(value) && closed);return value;
}
void changed(nr::present::FrameWait &wait){
    const auto end=GetTickCount64()+3000;
    while(GetTickCount64()<end){const auto wake=wait.wait(250);if(wake==nr::present::FrameWait::Wake::changed)return;messages();}
    throw std::runtime_error("change notification was lost");
}
void drain(nr::present::FrameWait &wait){
    for(unsigned n=0;n<64;++n){messages();if(wait.wait(0)==nr::present::FrameWait::Wake::timeout)return;}
    throw std::runtime_error("notification did not settle");
}
int wmain(int argc,wchar_t **argv){
    try{
        require(argc==2);const fs::path dir=argv[1];require(fs::create_directory(dir));
        struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{dir};
        const auto frame=dir/L"latest.bin",temporary=dir/L"next.bin";write(frame,1);
        nr::present::FrameWait wait(frame.c_str());messages();drain(wait);
        const auto began=GetTickCount64();require(wait.wait(30)==nr::present::FrameWait::Wake::timeout);require(GetTickCount64()-began>=20);
        write(temporary,2);require(MoveFileExW(temporary.c_str(),frame.c_str(),MOVEFILE_REPLACE_EXISTING));changed(wait);require(read(frame)==2);drain(wait);
        for(DWORD value=3;value<=10;++value){write(temporary,value);require(MoveFileExW(temporary.c_str(),frame.c_str(),MOVEFILE_REPLACE_EXISTING));}
        changed(wait);require(read(frame)==10);drain(wait);
        MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
        require(PostThreadMessageW(GetCurrentThreadId(),WM_APP+7,0,0));
        require(PeekMessageW(&message,nullptr,WM_APP+7,WM_APP+7,PM_NOREMOVE));
        require(wait.wait(250)==nr::present::FrameWait::Wake::message);messages();drain(wait);
        wait.close();bool rejected=false;try{wait.wait(0);}catch(const std::invalid_argument &){rejected=true;}require(rejected);
        std::printf("{\"passed\":true,\"atomic_replacement_exact\":true,\"coalesced_latest_exact\":true,\"seen_message_interrupts_wait\":true,\"bounded_timeout\":true,\"closed_wait_rejected\":true,\"notifications\":%llu,\"messages\":%llu,\"timeouts\":%llu,\"gpu_used\":false,\"desktop_changed\":false}\n",static_cast<unsigned long long>(wait.notifications),static_cast<unsigned long long>(wait.messages),static_cast<unsigned long long>(wait.timeouts));return 0;
    }catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
