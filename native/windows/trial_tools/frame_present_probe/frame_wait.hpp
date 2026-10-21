#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
namespace nr::present {
// Notifications are hints. The caller must still validate the complete frame,
// identity and monotonic sequence after every wakeup, including timeouts.
class FrameWait {
    HANDLE change=INVALID_HANDLE_VALUE;
public:
    uint64_t notifications=0,messages=0,timeouts=0;
    enum class Wake {changed,message,timeout};
    explicit FrameWait(const wchar_t *frame){
        const auto directory=std::filesystem::path(frame).parent_path();
        if(directory.empty())throw std::invalid_argument("frame wait requires an explicit directory");
        change=FindFirstChangeNotificationW(directory.c_str(),FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE);
        if(change==INVALID_HANDLE_VALUE)throw std::runtime_error("frame notification creation failed");
    }
    FrameWait(const FrameWait &)=delete;
    FrameWait &operator=(const FrameWait &)=delete;
    ~FrameWait(){if(change!=INVALID_HANDLE_VALUE)FindCloseChangeNotification(change);}
    void close(){
        if(change!=INVALID_HANDLE_VALUE){
            if(!FindCloseChangeNotification(change))throw std::runtime_error("frame notification close failed");
            change=INVALID_HANDLE_VALUE;
        }
    }
    Wake wait(DWORD milliseconds){
        if(change==INVALID_HANDLE_VALUE || milliseconds>250)throw std::invalid_argument("invalid frame notification wait");
        const auto status=MsgWaitForMultipleObjectsEx(1,&change,milliseconds,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if(status==WAIT_OBJECT_0){
            // Rearm only after this handle is signaled, never after a timeout
            // or a message. Events coalesce; they are not a frame counter.
            if(!FindNextChangeNotification(change))throw std::runtime_error("frame notification rearm failed");
            ++notifications;return Wake::changed;
        }
        if(status==WAIT_OBJECT_0+1){++messages;return Wake::message;}
        if(status==WAIT_TIMEOUT){++timeouts;return Wake::timeout;}
        throw std::runtime_error("frame notification/message wait failed");
    }
};
}
