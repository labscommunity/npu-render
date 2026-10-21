// Finite owned-window test of normal Windows input on an indirect primary.
// SendInput targets only this window; no arbitrary key or screen content is logged.
struct InputStimulus {
    static constexpr ULONG_PTR tag=0x4e505549;
    HWND window=nullptr,original_foreground=nullptr;POINT original_cursor{};bool cursor_saved=false,stopped=false,paint_failed=false;unsigned animation=0,step=0,clicks=0,keys=0,unicode_packets=0,relative_moves=0;bool relative_requested=false,relative_button_sent=false;POINT relative_origin{};bool unicode_pending=false;ULONGLONG unicode_pending_at=0;
    static void receipt(const char *kind,unsigned step){LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);std::printf("{\"input_event\":\"%s\",\"step\":%u,\"qpc\":%lld,\"owned_window\":true}\n",kind,step,qpc.QuadPart);std::fflush(stdout);}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto *self=reinterpret_cast<InputStimulus *>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<InputStimulus *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self && message==WM_CLOSE){self->stopped=true;return 0;}
        if(self && message==WM_MOUSEMOVE && self->relative_requested && !self->relative_moves && self->step==0){
            POINT point{};const auto extra=GetMessageExtraInfo();const bool available=GetCursorPos(&point)!=0;
            std::printf("{\"owned_relative_diagnostic\":true,\"tag_matches\":%s,\"cursor_available\":%s,\"cursor_changed\":%s,\"target_owned\":%s,\"message_x\":%d,\"message_y\":%d,\"cursor_x\":%ld,\"cursor_y\":%ld}\n",extra==tag?"true":"false",available?"true":"false",available && (point.x!=self->relative_origin.x || point.y!=self->relative_origin.y)?"true":"false",available && WindowFromPoint(point)==hwnd?"true":"false",static_cast<int>(static_cast<short>(LOWORD(l))),static_cast<int>(static_cast<short>(HIWORD(l))),point.x,point.y);std::fflush(stdout);
        }
        if(self && message==WM_MOUSEMOVE && self->relative_requested && !self->relative_moves && self->step==0 && GetMessageExtraInfo()==tag){
            POINT point{};if(GetCursorPos(&point) && WindowFromPoint(point)==hwnd && (point.x!=self->relative_origin.x || point.y!=self->relative_origin.y)){++self->relative_moves;receipt("relative_mouse_received",self->step);}
        }
        if(self && message==WM_LBUTTONUP && GetMessageExtraInfo()==tag && self->step==0){self->step=1;++self->clicks;receipt("mouse_received",self->step);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(self && self->step==1 && ((message==WM_CHAR && w==L'R') || ((message==WM_KEYDOWN || message==WM_KEYUP) && w==VK_PACKET))){
            std::printf("{\"owned_keyboard_diagnostic\":true,\"message\":%u,\"tag_matches\":%s,\"is_packet\":%s,\"is_expected_character\":%s,\"message_time\":%ld,\"message_parameter\":%lld}\n",message,GetMessageExtraInfo()==tag?"true":"false",w==VK_PACKET?"true":"false",w==L'R'?"true":"false",GetMessageTime(),static_cast<long long>(l));std::fflush(stdout);
        }
        if(self && message==WM_KILLFOCUS)self->unicode_pending=false;
        if(self && message==WM_KEYDOWN && self->step==1){
            self->unicode_pending=w==VK_PACKET && GetMessageExtraInfo()==tag && GetFocus()==hwnd;
            if(self->unicode_pending){++self->unicode_packets;self->unicode_pending_at=GetTickCount64();receipt("unicode_packet_received",self->step);}
        }
        if(self && message==WM_CHAR && self->step==1){
            const bool expected=w==L'R' && self->unicode_pending && GetTickCount64()-self->unicode_pending_at<=2000 && GetFocus()==hwnd;
            self->unicode_pending=false;
            if(expected){self->step=2;++self->keys;receipt("key_received",self->step);InvalidateRect(hwnd,nullptr,FALSE);}
            return 0;
        }
        if(self && message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(hwnd,&paint);RECT rect{};GetClientRect(hwnd,&rect);const COLORREF colors[]{RGB(30,60,180),RGB(20,180,60),RGB(180,30,80)};const unsigned color_step=self->step==2 && self->animation?(self->animation-1)%3:self->step;const auto brush=CreateSolidBrush(colors[color_step]);FillRect(dc,&rect,brush);DeleteObject(brush);EndPaint(hwnd,&paint);if(!GdiFlush())self->paint_failed=true;receipt("painted",self->step);LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);std::printf("{\"stimulus_paint_color\":%u,\"animation_sequence\":%u,\"qpc\":%lld}\n",color_step,self->animation,stamp.QuadPart);std::fflush(stdout);return 0;}
        return DefWindowProcW(hwnd,message,w,l);
    }
    explicit InputStimulus(const RECT &area){
        original_foreground=GetForegroundWindow();cursor_saved=GetCursorPos(&original_cursor)!=0;
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"NpuRenderPrimaryInputStimulus";require(RegisterClassW(&cls)!=0,"input class failed");
        require(area.left==0 && area.top==0 && area.right>=640 && area.bottom>=384,"input needs indirect primary");
        window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"NPU desktop input test",WS_POPUP,64,64,512,256,nullptr,nullptr,cls.hInstance,this);require(window!=nullptr,"input window failed");
    }
    ~InputStimulus(){if(window){if(GetForegroundWindow()==window && original_foreground && IsWindow(original_foreground))SetForegroundWindow(original_foreground);ShowWindow(window,SW_HIDE);DestroyWindow(window);}if(cursor_saved)SetCursorPos(original_cursor.x,original_cursor.y);}
    void neutral(){for(const int key:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN,VK_LBUTTON,VK_RBUTTON,VK_MBUTTON})require(!(GetAsyncKeyState(key)&0x8000),"user input active during owned test");}
    void click(){
        neutral();require(!relative_requested && SetCursorPos(290,170)!=0,"relative mouse origin failed");require(GetCursorPos(&relative_origin) && relative_origin.x==290 && relative_origin.y==170 && WindowFromPoint(relative_origin)==window,"relative mouse origin not owned");
        INPUT move{};move.type=INPUT_MOUSE;move.mi.dx=5;move.mi.dy=5;move.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_MOVE_NOCOALESCE;move.mi.dwExtraInfo=tag;relative_requested=true;receipt("relative_mouse_sent",step);require(SendInput(1,&move,sizeof(INPUT))==1,"relative mouse injection failed");
    }
    void continue_relative_click(){
        if(!relative_requested || relative_moves!=1 || relative_button_sent)return;
        neutral();POINT point{};require(GetCursorPos(&point) && WindowFromPoint(point)==window && (point.x!=relative_origin.x || point.y!=relative_origin.y),"relative click target not owned");
        INPUT inputs[2]{};for(auto &x:inputs){x.type=INPUT_MOUSE;x.mi.dwExtraInfo=tag;}inputs[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;inputs[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;receipt("mouse_sent",step);require(SendInput(2,inputs,sizeof(INPUT))==2,"owned mouse injection failed");relative_button_sent=true;
    }
    void key(){
        neutral();require(GetForegroundWindow()==window && GetFocus()==window,"owned keyboard focus absent");INPUT inputs[2]{};for(auto &x:inputs){x.type=INPUT_KEYBOARD;x.ki.wScan=L'R';x.ki.dwFlags=KEYEVENTF_UNICODE;x.ki.dwExtraInfo=tag;}inputs[1].ki.dwFlags|=KEYEVENTF_KEYUP;receipt("key_sent",step);require(SendInput(2,inputs,sizeof(INPUT))==2,"owned key injection failed");
    }
};
int input_stimulus(const wchar_t *directory,const wchar_t *duration){
    wchar_t *end=nullptr;const auto seconds=std::wcstoul(duration,&end,10);require(end!=duration && !*end && seconds>=1 && seconds<=14400,"input duration rejected");require(nr::present::current_console_default(),"input requires Default console");
    require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=nullptr,"input DPI unavailable");const auto route=nr::live::separated();InputStimulus window(route.indirect);ShowWindow(window.window,SW_SHOWNOACTIVATE);UpdateWindow(window.window);const auto start=GetTickCount64();bool click_sent=false,key_sent=false;unsigned refresh=0;
    const std::wstring root(directory);const auto cue=[&](const wchar_t *name){return GetFileAttributesW((root+L"\\"+name).c_str())!=INVALID_FILE_ATTRIBUTES;};
    std::printf("{\"input_ready\":true,\"pid\":%lu,\"indirect_primary\":true,\"cursor_confined\":false}\n",GetCurrentProcessId());std::fflush(stdout);
    while(!window.stopped && GetTickCount64()-start<uint64_t(seconds)*1000){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)window.stopped=true;TranslateMessage(&message);DispatchMessageW(&message);}
        require(nr::present::current_console_default() && nr::live::same(route,nr::live::separated()),"input desktop changed");
        require(!window.paint_failed,"owned GDI batch flush failed");const auto tick=unsigned((GetTickCount64()-start)/15000);if(tick!=refresh){refresh=tick;if(window.step==2 && cue(L"animate"))++window.animation;InvalidateRect(window.window,nullptr,FALSE);UpdateWindow(window.window);}
        if(!click_sent && cue(L"click")){window.click();click_sent=true;}
        window.continue_relative_click();
        if(!key_sent && window.step==1 && cue(L"key")){window.key();key_sent=true;}
        Sleep(50);
    }
    const bool passed=window.clicks==1 && window.keys==1 && window.unicode_packets==1 && window.relative_moves==1 && window.relative_button_sent && window.step==2;
    std::printf("{\"completed\":true,\"input_stimulus_only\":true,\"passed\":%s,\"mouse_receipts\":%u,\"key_receipts\":%u,\"desktop_offloaded\":false}\n",passed?"true":"false",window.clicks,window.keys);return passed?0:1;
}

int input_contract(){
    require(nr::present::current_console_default(),"input contract requires Default console");require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=nullptr,"input contract DPI unavailable");
    const auto monitor=MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);MONITORINFO mi{};mi.cbSize=sizeof(mi);require(monitor && GetMonitorInfoW(monitor,&mi) && (mi.dwFlags&MONITORINFOF_PRIMARY),"input contract primary unavailable");
    InputStimulus window(mi.rcMonitor);require(SetWindowPos(window.window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)!=0,"input contract exposure failed");UpdateWindow(window.window);
    const auto start=GetTickCount64();bool clicked=false,keyed=false;
    while(GetTickCount64()-start<12000 && window.step!=2){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        require(nr::present::current_console_default(),"input contract desktop changed");
        if(!clicked && GetTickCount64()-start>500){window.click();clicked=true;}
        window.continue_relative_click();
        if(!keyed && window.step==1){
            SendMessageW(window.window,WM_CHAR,L'R',1);require(window.step==1 && window.keys==0,"unauthenticated character accepted");
            std::printf("{\"unauthenticated_character_rejected\":true}\n");std::fflush(stdout);window.key();keyed=true;
        }
        Sleep(50);
    }
    SendMessageW(window.window,WM_CHAR,L'R',1);require(window.keys<=1,"duplicate character accepted");
    const bool passed=window.clicks==1 && window.keys==1 && window.unicode_packets==1 && window.relative_moves==1 && window.relative_button_sent && window.step==2;std::printf("{\"input_contract\":true,\"completed\":true,\"passed\":%s,\"mouse_receipts\":%u,\"key_receipts\":%u,\"npu_tested\":false}\n",passed?"true":"false",window.clicks,window.keys);return passed?0:1;
}
