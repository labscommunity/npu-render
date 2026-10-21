#pragma once
// Preserve ordinary top-level application placements during the finite primary
// topology experiment. No titles, document content or screen pixels are read.
struct WindowPlacements {
    struct Entry {HWND window;DWORD pid;HANDLE process;WINDOWPLACEMENT placement;};
    std::vector<Entry> entries;POINT cursor{};HWND foreground=nullptr;bool cursor_saved=false;
    static BOOL CALLBACK collect(HWND window,LPARAM parameter){
        auto &self=*reinterpret_cast<WindowPlacements *>(parameter);
        if(!IsWindowVisible(window) || (GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION)!=WS_CAPTION || (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW))return TRUE;
        DWORD pid=0,session=0,current=0;GetWindowThreadProcessId(window,&pid);
        if(!pid || !ProcessIdToSessionId(pid,&session) || !ProcessIdToSessionId(GetCurrentProcessId(),&current) || session!=current)return TRUE;
        const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)return TRUE;
        WINDOWPLACEMENT placement{};placement.length=sizeof(placement);
        if(!GetWindowPlacement(window,&placement)){CloseHandle(process);return TRUE;}
        self.entries.push_back({window,pid,process,placement});return TRUE;
    }
    WindowPlacements(){
        if(!SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))throw std::runtime_error("placement DPI context unavailable");
        cursor_saved=GetCursorPos(&cursor)!=0;foreground=GetForegroundWindow();if(!EnumWindows(collect,reinterpret_cast<LPARAM>(this)))throw std::runtime_error("window snapshot failed");
        for(const auto &e:entries){FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(e.process,&created,&exited,&kernel,&user))throw std::runtime_error("placement owner creation time unavailable");const auto &p=e.placement;
            std::printf("{\"window_placement_snapshot\":true,\"hwnd\":%llu,\"pid\":%lu,\"created\":%llu,\"flags\":%u,\"show\":%u,\"normal\":[%ld,%ld,%ld,%ld],\"minimum\":[%ld,%ld],\"maximum\":[%ld,%ld]}\n",static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(e.window)),e.pid,(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime,p.flags,p.showCmd,p.rcNormalPosition.left,p.rcNormalPosition.top,p.rcNormalPosition.right,p.rcNormalPosition.bottom,p.ptMinPosition.x,p.ptMinPosition.y,p.ptMaxPosition.x,p.ptMaxPosition.y);}
        std::printf("{\"window_snapshot_complete\":true,\"windows\":%zu,\"cursor_saved\":%s,\"cursor\":[%ld,%ld],\"foreground\":%llu,\"titles_or_content_recorded\":false}\n",entries.size(),cursor_saved?"true":"false",cursor.x,cursor.y,static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(foreground)));std::fflush(stdout);
    }
    WindowPlacements(const WindowPlacements &)=delete;
    ~WindowPlacements(){for(const auto &entry:entries)CloseHandle(entry.process);}
    bool restore(){
        unsigned restored=0,closed=0,failed=0;
        for(const auto &entry:entries){DWORD pid=0;GetWindowThreadProcessId(entry.window,&pid);
            if(!IsWindow(entry.window) || pid!=entry.pid || WaitForSingleObject(entry.process,0)!=WAIT_TIMEOUT){++closed;continue;}
            WINDOWPLACEMENT after{};after.length=sizeof(after);
            if(!SetWindowPlacement(entry.window,&entry.placement) || !GetWindowPlacement(entry.window,&after) || after.showCmd!=entry.placement.showCmd || !EqualRect(&after.rcNormalPosition,&entry.placement.rcNormalPosition)){++failed;continue;}++restored;
        }
        POINT actual{};const bool cursor_ok=!cursor_saved || (SetCursorPos(cursor.x,cursor.y)!=0 && GetCursorPos(&actual) && actual.x==cursor.x && actual.y==cursor.y);
        const bool focus_ok=!foreground || !IsWindow(foreground) || GetForegroundWindow()==foreground || (SetForegroundWindow(foreground)!=0 && GetForegroundWindow()==foreground);
        std::printf("{\"window_placement_recovery\":true,\"captured\":%zu,\"restored\":%u,\"closed_during_trial\":%u,\"failed\":%u,\"cursor_restored\":%s,\"foreground_restored\":%s,\"titles_or_content_recorded\":false}\n",entries.size(),restored,closed,failed,cursor_ok?"true":"false",focus_ok?"true":"false");std::fflush(stdout);
        return !failed && cursor_ok && focus_ok;
    }
};
