// Finite measurements on an owned window; no arbitrary screen or input capture.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>
#include "session_gate.hpp"
void require(bool yes,const char *text){if(!yes)throw std::runtime_error(text);}
#include "live_topology.hpp"
long long stamp(){LARGE_INTEGER q{};require(QueryPerformanceCounter(&q)!=0,"QPC unavailable");return q.QuadPart;}
COLORREF color(unsigned sequence){return RGB(32+(sequence&127u),48+((sequence>>7u)&127u),64+((sequence>>14u)&127u));}
struct Window {
    static constexpr ULONG_PTR tag=0x4e505552;
    HWND handle=nullptr,original_foreground=nullptr;
    POINT original_cursor{};bool cursor_saved=false,stopped=false,paint_failed=false,pending=false,key_pending=false;
    unsigned sequence=0,target=0,received=0,mouse=0,keyboard=0,packets=0,pending_rejections=0;
    ULONGLONG key_tick=0;
    static void emit(const char *kind,unsigned value){
        const auto c=color(value);
        std::printf("{\"repeat_event\":\"%s\",\"sequence\":%u,\"qpc\":%lld,\"bgra\":[%u,%u,%u,255],\"owned_window\":true}\n",kind,value,stamp(),unsigned(GetBValue(c)),unsigned(GetGValue(c)),unsigned(GetRValue(c)));std::fflush(stdout);
    }
    void accept(bool key){sequence=target;pending=false;key_pending=false;++received;if(key)++keyboard;else ++mouse;emit(key?"key_received":"mouse_received",sequence);InvalidateRect(handle,nullptr,FALSE);}
    static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
        auto *s=reinterpret_cast<Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){s=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
        if(s && m==WM_CLOSE){s->stopped=true;return 0;}
        if(s && m==WM_KILLFOCUS)s->key_pending=false;
        if(s && m==WM_LBUTTONUP && s->pending && (s->target&1u) && GetMessageExtraInfo()==tag){
            POINT p{};if(GetCursorPos(&p) && WindowFromPoint(p)==h && GetForegroundWindow()==h && GetFocus()==h)s->accept(false);return 0;
        }
        if(s && m==WM_KEYDOWN && s->pending && !(s->target&1u)){
            s->key_pending=w==VK_PACKET && GetMessageExtraInfo()==tag && GetFocus()==h && GetForegroundWindow()==h;
            if(s->key_pending){s->key_tick=GetTickCount64();++s->packets;emit("unicode_packet_received",s->target);}return 0;
        }
        if(s && m==WM_CHAR){
            const bool accepted=s->pending && !(s->target&1u) && w==L'R' && s->key_pending && GetTickCount64()-s->key_tick<=2000 && GetFocus()==h && GetForegroundWindow()==h;
            s->key_pending=false;if(accepted)s->accept(true);return 0;
        }
        if(s && m==WM_PAINT){
            PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);auto brush=CreateSolidBrush(color(s->sequence));
            const bool painted=brush && FillRect(dc,&rect,brush)!=0;if(brush)DeleteObject(brush);EndPaint(h,&paint);
            if(!painted || !GdiFlush())s->paint_failed=true;emit("painted",s->sequence);return 0;
        }
        return DefWindowProcW(h,m,w,l);
    }
    Window(){
        original_foreground=GetForegroundWindow();cursor_saved=GetCursorPos(&original_cursor)!=0;
        WNDCLASSW c{};c.lpfnWndProc=procedure;c.hInstance=GetModuleHandleW(nullptr);c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.lpszClassName=L"NpuRepeatedOwnedInput";require(RegisterClassW(&c)!=0,"class registration failed");
        handle=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,c.lpszClassName,L"NPU owned repeated input",WS_POPUP,64,64,512,256,nullptr,nullptr,c.hInstance,this);require(handle!=nullptr,"window creation failed");
        ShowWindow(handle,SW_SHOWNOACTIVATE);UpdateWindow(handle);
    }
    ~Window(){if(handle){if(GetForegroundWindow()==handle && original_foreground && IsWindow(original_foreground))SetForegroundWindow(original_foreground);ShowWindow(handle,SW_HIDE);DestroyWindow(handle);}if(cursor_saved)SetCursorPos(original_cursor.x,original_cursor.y);}
    void neutral(){for(const int key:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN,VK_LBUTTON,VK_RBUTTON,VK_MBUTTON})require(!(GetAsyncKeyState(key)&0x8000),"user input active during owned test");}
    void inject(bool contract){
        require(!pending && sequence==received,"input sequencing failed");neutral();require(nr::present::current_console_default(),"input desktop changed");
        target=received+1;pending=true;INPUT inputs[2]{};
        if(target&1u){
            require(SetCursorPos(300,180)!=0,"owned pointer placement failed");POINT p{};require(GetCursorPos(&p) && WindowFromPoint(p)==handle,"mouse target is not owned");
            for(auto &i:inputs){i.type=INPUT_MOUSE;i.mi.dwExtraInfo=tag;}inputs[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;inputs[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;emit("mouse_sent",target);
        }else{
            require(GetForegroundWindow()==handle && GetFocus()==handle,"keyboard target is not owned");
            for(auto &i:inputs){i.type=INPUT_KEYBOARD;i.ki.wScan=L'R';i.ki.dwFlags=KEYEVENTF_UNICODE;i.ki.dwExtraInfo=tag;}inputs[1].ki.dwFlags|=KEYEVENTF_KEYUP;emit("key_sent",target);
        }
        if(contract){
            const unsigned before=received,before_packets=packets;const auto extra=SetMessageExtraInfo(0);
            SendMessageW(handle,WM_LBUTTONUP,0,MAKELPARAM(236,116));SendMessageW(handle,WM_KEYDOWN,VK_PACKET,1);SendMessageW(handle,WM_CHAR,L'R',1);SetMessageExtraInfo(extra);
            require(received==before && packets==before_packets && pending && !key_pending,"untagged pending input accepted");++pending_rejections;
        }
        require(SendInput(2,inputs,sizeof(INPUT))==2,"owned input injection failed");
    }
    void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT)stopped=true;TranslateMessage(&m);DispatchMessageW(&m);}require(!paint_failed,"GDI paint failed");}
};
unsigned number(const wchar_t *text,unsigned minimum,unsigned maximum){wchar_t *end=nullptr;auto n=std::wcstoul(text,&end,10);require(end!=text && !*end && n>=minimum && n<=maximum,"argument out of range");return unsigned(n);}
int wmain(int argc,wchar_t **argv)try{
    const bool contract=argc==2 && !std::wcscmp(argv[1],L"--contract");
    require(contract || (argc==5 && !std::wcscmp(argv[1],L"--run")),"expected --contract or --run <cue-directory> <seconds> <even-count>");
    require(nr::present::current_console_default(),"Default console required");require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=nullptr,"DPI unavailable");
    const unsigned count=contract?8:number(argv[4],4,100),seconds=contract?25:number(argv[3],10,2100);require(!(count&1u),"even input count required");
    const std::wstring root=contract?L"":argv[2];const auto cue=[&](const std::wstring &name){return !root.empty() && GetFileAttributesW((root+L"\\"+name).c_str())!=INVALID_FILE_ATTRIBUTES;};
    nr::live::Route route{};if(!contract)route=nr::live::separated();
    if(!contract)require(route.indirect.left==0 && route.indirect.top==0,"indirect primary required");
    MONITORINFO mi{};mi.cbSize=sizeof(mi);require(GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&mi) && mi.rcMonitor.left==0 && mi.rcMonitor.top==0 && mi.rcMonitor.right>=640 && mi.rcMonitor.bottom>=384,"window area unavailable");
    LARGE_INTEGER frequency{};require(QueryPerformanceFrequency(&frequency)!=0,"QPC frequency unavailable");
    Window window;std::printf("{\"repeat_ready\":true,\"pid\":%lu,\"count\":%u,\"qpc_frequency\":%lld,\"contract_only\":%s}\n",GetCurrentProcessId(),count,frequency.QuadPart,contract?"true":"false");std::fflush(stdout);
    const auto start=GetTickCount64();auto health=start;unsigned rejected=0,last_checked=0;bool animation_started=false,animation_done=false;long long animation_start=0;unsigned animation_next=0,animation_emitted=0,animation_skipped=0;
    while(!window.stopped && GetTickCount64()-start<uint64_t(seconds)*1000){
        window.pump();const auto now=GetTickCount64();
        if(now-health>=100){require(nr::present::current_console_default(),"input desktop changed");if(!contract)require(nr::live::same(route,nr::live::separated()),"input topology changed");health=now;}
        if(contract && last_checked==window.received && !window.pending){
            SendMessageW(window.handle,WM_CHAR,L'R',1);SendMessageW(window.handle,WM_LBUTTONUP,0,MAKELPARAM(236,116));
            require(window.received==last_checked,"unsolicited message advanced input");++rejected;++last_checked;
        }
        if(window.received<count && !window.pending && ((contract && now-start>500) || cue(L"advance-"+std::to_wstring(window.received))))window.inject(contract);
        if(contract && window.received==count)break;
        if(!contract && window.received==count && !animation_started && cue(L"animation-start")){animation_started=true;animation_start=stamp();std::printf("{\"animation_clock\":true,\"start_qpc\":%lld,\"target_hz\":30,\"duration_seconds\":10}\n",animation_start);std::fflush(stdout);}
        if(animation_started && !animation_done){
            const auto q=stamp();const auto due=unsigned((q-animation_start)*30/frequency.QuadPart);
            if(due>=300){animation_skipped+=300-animation_next;animation_done=true;std::printf("{\"animation_completed\":true,\"emitted\":%u,\"skipped_scheduled_updates\":%u,\"duration_seconds\":10,\"target_hz\":30}\n",animation_emitted,animation_skipped);std::fflush(stdout);}
            else if(due>=animation_next){animation_skipped+=due-animation_next;animation_next=due+1;++animation_emitted;window.sequence=1000+due;Window::emit("animation_requested",window.sequence);InvalidateRect(window.handle,nullptr,FALSE);UpdateWindow(window.handle);}
        }
        if(!contract && cue(L"stop"))break;
        MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    window.pump();const bool passed=window.received==count && window.mouse==count/2 && window.keyboard==count/2 && window.packets==count/2 && (!contract || rejected==count+1 && window.pending_rejections==count) && (!animation_started || animation_done);
    std::printf("{\"completed\":true,\"passed\":%s,\"repeated_input\":true,\"contract_only\":%s,\"received\":%u,\"mouse\":%u,\"keyboard\":%u,\"unicode_packets\":%u,\"unsolicited_message_checks\":%u,\"animation_emitted\":%u,\"animation_skipped\":%u,\"npu_execution_proven\":false,\"physical_display_proven\":false}\n",passed?"true":"false",contract?"true":"false",window.received,window.mouse,window.keyboard,window.packets,rejected,animation_emitted,animation_skipped);return passed?0:1;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
