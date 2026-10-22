#pragma once
// Optional finite pointer mode. The controller must arm a separate interactive
// recovery task before publishing the cue. Existing caller confinement is refused.
struct CursorConfinement {
    RECT desired{};POINT original{};bool active=false,released=false;unsigned reacquired=0;std::wstring cue;
    static RECT desktop(){const LONG x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN);return {x,y,x+GetSystemMetrics(SM_CXVIRTUALSCREEN),y+GetSystemMetrics(SM_CYVIRTUALSCREEN)};}
    CursorConfinement(RECT target,const wchar_t *path):desired(target),cue(path){
        RECT before{},full=desktop();if(!GetClipCursor(&before) || !EqualRect(&before,&full) || !GetCursorPos(&original))throw std::runtime_error("cursor mode requires an unconfined pointer");
        FILETIME created{},exited{},kernel{},user{};DWORD session=0;
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user) || !ProcessIdToSessionId(GetCurrentProcessId(),&session))throw std::runtime_error("cursor owner identity unavailable");
        std::printf("{\"cursor_guard_snapshot\":true,\"schema\":1,\"pid\":%lu,\"created\":%llu,\"session\":%lu,\"original_unconfined\":true,\"original_cursor\":[%ld,%ld],\"desired_clip\":[%ld,%ld,%ld,%ld]}\n",GetCurrentProcessId(),(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime,session,original.x,original.y,desired.left,desired.top,desired.right,desired.bottom);std::fflush(stdout);
    }
    CursorConfinement(const CursorConfinement &)=delete;
    ~CursorConfinement(){if(active)ClipCursor(nullptr);}
    void poll(){
        if(!active && GetFileAttributesW(cue.c_str())!=INVALID_FILE_ATTRIBUTES){
            RECT full=desktop(),before{};if(!GetClipCursor(&before) || !EqualRect(&before,&full))throw std::runtime_error("pointer became confined before admission");
            if(!ClipCursor(&desired))throw std::runtime_error("cursor confinement failed");active=true;
            std::printf("{\"cursor_confined\":true,\"pid\":%lu,\"clip\":[%ld,%ld,%ld,%ld]}\n",GetCurrentProcessId(),desired.left,desired.top,desired.right,desired.bottom);std::fflush(stdout);
        }
        if(active){RECT actual{};if(!GetClipCursor(&actual))throw std::runtime_error("cursor confinement query failed");if(!EqualRect(&actual,&desired)){
            const RECT full=desktop();DWORD foreground_pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
            std::printf("{\"cursor_clip_changed\":true,\"actual\":[%ld,%ld,%ld,%ld],\"was_unconfined\":%s,\"foreground_pid\":%lu}\n",actual.left,actual.top,actual.right,actual.bottom,EqualRect(&actual,&full)?"true":"false",foreground_pid);std::fflush(stdout);
            if(!EqualRect(&actual,&full)){active=false;throw std::runtime_error("different cursor confinement appeared");}
            // Foreground transitions may release the shared cursor constraint.
            // The finite admitted mode restores only an unrestricted pointer;
            // another application's different rectangle remains untouched.
            if(!ClipCursor(&desired) || !GetClipCursor(&actual) || !EqualRect(&actual,&desired))throw std::runtime_error("cursor confinement reacquisition failed");
            ++reacquired;std::printf("{\"cursor_reacquired_after_release\":true,\"count\":%u,\"foreground_pid\":%lu}\n",reacquired,foreground_pid);std::fflush(stdout);
        }}
    }
    bool release(bool restore_position=false){
        bool ok=true;if(active){RECT current{};ok=GetClipCursor(&current)&&EqualRect(&current,&desired);if(ok)ok=ClipCursor(nullptr)!=0;active=false;}RECT actual{},full=desktop();ok=ok&&GetClipCursor(&actual)&&EqualRect(&actual,&full);
        if(restore_position){POINT point{};ok=ok&&SetCursorPos(original.x,original.y)&&GetCursorPos(&point)&&point.x==original.x&&point.y==original.y;}
        released=ok;std::printf("{\"cursor_confinement_recovery\":true,\"passed\":%s,\"original_unconfined_restored\":%s,\"position_restore_requested\":%s}\n",ok?"true":"false",ok?"true":"false",restore_position?"true":"false");std::fflush(stdout);return ok;
    }
};
