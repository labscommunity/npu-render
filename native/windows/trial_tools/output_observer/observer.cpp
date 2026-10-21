// Independent diagnostic: two pixels from the physical output only. This adds
// GPU copy work and does not prove physical scanout or NPU provenance.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include "session_gate.hpp"
using Microsoft::WRL::ComPtr;
void require(bool yes,const char *message){if(!yes)throw std::runtime_error(message);}
void check(HRESULT hr){if(FAILED(hr)){char text[64];std::snprintf(text,sizeof(text),"DXGI observer failure %08lx",static_cast<unsigned long>(hr));throw std::runtime_error(text);}}
#include "live_topology.hpp"
long long qpc(){LARGE_INTEGER q{};require(QueryPerformanceCounter(&q)!=0,"QPC unavailable");return q.QuadPart;}
const char *json_bool(bool b){return b?"true":"false";}
struct Owner {HWND window{};DWORD pid{};};
Owner owner(POINT point){Owner o;o.window=WindowFromPoint(point);GetWindowThreadProcessId(o.window,&o.pid);return o;}
struct Peer {
    HANDLE handle=nullptr;DWORD pid=0;
    explicit Peer(DWORD id):pid(id){handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id);require(handle!=nullptr,"presenter process unavailable");verify();}
    ~Peer(){if(handle)CloseHandle(handle);}
    void verify() const {require(WaitForSingleObject(handle,0)==WAIT_TIMEOUT && GetProcessId(handle)==pid,"presenter process exited");}
};
struct Observation {
    std::array<bool,2> blue{},green{},red{},owned{};
    std::array<std::array<unsigned char,4>,2> bgra{};
    long long begin=0,end=0,present=0;UINT accumulated=0;
};
class Reader {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;ComPtr<IDXGIOutput1> output;
    std::array<ComPtr<ID3D11Texture2D>,2> staging;
    RECT rectangle{};std::array<POINT,2> points{};
public:
    unsigned long long copies=0,timeouts=0;unsigned reconnects=0;bool lost=false;
    Reader(RECT rect,LUID luid,std::array<POINT,2> locations):rectangle(rect),points(locations){
        ComPtr<IDXGIFactory1> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;!adapter;++i){ComPtr<IDXGIAdapter1> a;const auto hr=factory->EnumAdapters1(i,&a);if(hr==DXGI_ERROR_NOT_FOUND)break;check(hr);DXGI_ADAPTER_DESC1 d{};check(a->GetDesc1(&d));if((d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) || !nr::live::equal(d.AdapterLuid,luid))continue;
            for(UINT j=0;;++j){ComPtr<IDXGIOutput> o;const auto h=a->EnumOutputs(j,&o);if(h==DXGI_ERROR_NOT_FOUND)break;check(h);DXGI_OUTPUT_DESC d2{};check(o->GetDesc(&d2));if(d2.AttachedToDesktop && EqualRect(&d2.DesktopCoordinates,&rectangle)){require(d2.Rotation==DXGI_MODE_ROTATION_IDENTITY,"rotated output unsupported");adapter=a;check(o.As(&output));break;}}}
        require(adapter && output,"pinned physical output unavailable");
        ComPtr<IDXGIOutput6> output6;check(output.As(&output6));DXGI_OUTPUT_DESC1 od{};check(output6->GetDesc1(&od));require(od.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 && od.BitsPerColor==8,"observer requires existing 8-bit SDR");
        D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,&level,&context));
        check(output->DuplicateOutput(device.Get(),&duplication));DXGI_OUTDUPL_DESC dd{};duplication->GetDesc(&dd);
        require(dd.ModeDesc.Format==DXGI_FORMAT_B8G8R8A8_UNORM && dd.ModeDesc.Width==UINT(rect.right-rect.left) && dd.ModeDesc.Height==UINT(rect.bottom-rect.top) && dd.Rotation==DXGI_MODE_ROTATION_IDENTITY,"duplication mode mismatch");
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        for(size_t i=0;i<2;++i){require(PtInRect(&rect,points[i])!=0,"sample outside physical output");check(device->CreateTexture2D(&td,nullptr,&staging[i]));}
        std::printf("{\"observer_ready\":true,\"adapter_luid_low\":%lu,\"adapter_luid_high\":%ld,\"physical_x\":%ld,\"physical_y\":%ld,\"physical_width\":%ld,\"physical_height\":%ld,\"screenshots_saved\":false,\"gpu_copy_measurement_only\":true}\n",luid.LowPart,luid.HighPart,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top);std::fflush(stdout);
    }
    // Desktop duplication ends with DXGI_ERROR_ACCESS_LOST at every desktop switch and mode change; the
    // caller re-creates it once the Default desktop is back.
    void reconnect(){duplication.Reset();check(output->DuplicateOutput(device.Get(),&duplication));DXGI_OUTDUPL_DESC dd{};duplication->GetDesc(&dd);
        require(dd.ModeDesc.Format==DXGI_FORMAT_B8G8R8A8_UNORM && dd.ModeDesc.Width==UINT(rectangle.right-rectangle.left) && dd.ModeDesc.Height==UINT(rectangle.bottom-rectangle.top) && dd.Rotation==DXGI_MODE_ROTATION_IDENTITY,"duplication mode mismatch after reconnect");
        lost=false;++reconnects;std::printf("{\"duplication_reconnected\":true,\"count\":%u}\n",reconnects);std::fflush(stdout);}
    bool sample(DWORD expected,Observation &o){
        o={};o.begin=qpc();std::array<Owner,2> before{owner(points[0]),owner(points[1])};
        DXGI_OUTDUPL_FRAME_INFO info{};ComPtr<IDXGIResource> resource;auto hr=duplication->AcquireNextFrame(100,&info,&resource);
        if(hr==DXGI_ERROR_WAIT_TIMEOUT){++timeouts;return false;}if(hr==DXGI_ERROR_ACCESS_LOST){lost=true;return false;}check(hr);
        bool held=true;
        try{
            ComPtr<ID3D11Texture2D> texture;check(resource.As(&texture));D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);require(td.Width==UINT(rectangle.right-rectangle.left) && td.Height==UINT(rectangle.bottom-rectangle.top) && td.Format==DXGI_FORMAT_B8G8R8A8_UNORM,"acquired surface changed");
            for(size_t i=0;i<2;++i){const UINT x=UINT(points[i].x-rectangle.left),y=UINT(points[i].y-rectangle.top);D3D11_BOX box{x,y,0,x+1,y+1,1};context->CopySubresourceRegion(staging[i].Get(),0,0,0,0,texture.Get(),0,&box);++copies;}
            context->Flush();
            for(size_t i=0;i<2;++i){D3D11_MAPPED_SUBRESOURCE mapped{};const auto deadline=GetTickCount64()+2000;
                do{hr=context->Map(staging[i].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);if(hr==DXGI_ERROR_WAS_STILL_DRAWING){require(GetTickCount64()<deadline,"pixel readback deadline exceeded");Sleep(1);}}while(hr==DXGI_ERROR_WAS_STILL_DRAWING);check(hr);
                require(mapped.pData && mapped.RowPitch>=4,"pixel readback invalid");const auto *p=static_cast<const unsigned char *>(mapped.pData);
                std::array<unsigned char,4> pixel{p[0],p[1],p[2],p[3]};
                o.blue[i]=p[0]==180 && p[1]==60 && p[2]==30;o.green[i]=p[0]==60 && p[1]==180 && p[2]==20;o.red[i]=p[0]==80 && p[1]==30 && p[2]==180;context->Unmap(staging[i].Get(),0);
                const auto after=owner(points[i]);o.owned[i]=before[i].pid==expected && after.pid==expected && before[i].window && before[i].window==after.window;
                if(o.owned[i])o.bgra[i]=pixel;
            }
            resource.Reset();texture.Reset();hr=duplication->ReleaseFrame();held=false;check(hr);
            o.end=qpc();o.present=info.LastPresentTime.QuadPart;o.accumulated=info.AccumulatedFrames;return true;
        }catch(...){if(held)duplication->ReleaseFrame();throw;}
    }
    void emit(const Observation &o){
        std::printf("{\"qpc\":%lld,\"read_completed_qpc\":%lld,\"last_present_qpc\":%lld,\"accumulated_frames\":%u,\"ownership_verified_before_and_after\":true,\"points\":[",o.begin,o.end,o.present,o.accumulated);
        for(size_t i=0;i<2;++i)std::printf("%s{\"x\":%ld,\"y\":%ld,\"owned_presenter_at_point\":%s,\"known_blue\":%s,\"known_green\":%s,\"known_red\":%s,\"owned_bgra_or_zero\":[%u,%u,%u,%u]}",i?",":"",points[i].x,points[i].y,json_bool(o.owned[i]),json_bool(o.blue[i]),json_bool(o.green[i]),json_bool(o.red[i]),unsigned(o.bgra[i][0]),unsigned(o.bgra[i][1]),unsigned(o.bgra[i][2]),unsigned(o.bgra[i][3]));
        std::printf("]}\n");std::fflush(stdout);
    }
};
struct TestWindow {
    HWND handle=nullptr;unsigned step=0;
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto *s=reinterpret_cast<TestWindow *>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){s=static_cast<TestWindow *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
        if(s && m==WM_PAINT){PAINTSTRUCT p{};const auto dc=BeginPaint(h,&p);RECT rect{};GetClientRect(h,&rect);const COLORREF colors[]{RGB(30,60,180),RGB(20,180,60),RGB(180,30,80)};auto brush=CreateSolidBrush(colors[s->step]);FillRect(dc,&rect,brush);DeleteObject(brush);EndPaint(h,&p);return 0;}return DefWindowProcW(h,m,w,l);}
    TestWindow(){WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"NpuPhysicalObserverTest";require(RegisterClassW(&c)!=0,"test window registration failed");handle=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,c.lpszClassName,L"NPU output observer qualification",WS_POPUP,64,64,512,256,nullptr,nullptr,c.hInstance,this);require(handle!=nullptr,"test window failed");ShowWindow(handle,SW_SHOWNOACTIVATE);}
    ~TestWindow(){if(handle){ShowWindow(handle,SW_HIDE);DestroyWindow(handle);}}
    void paint(unsigned n){step=n;InvalidateRect(handle,nullptr,FALSE);UpdateWindow(handle);require(GdiFlush()!=0,"test flush failed");}
    void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
};
void self_test(){
    UINT pc=0,mc=0;require(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pc,&mc)==ERROR_SUCCESS && pc==1 && mc<=256,"self-test requires ordinary single output");std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);check(HRESULT_FROM_WIN32(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pc,paths.data(),&mc,modes.data(),nullptr)));require(pc==1 && paths[0].targetInfo.outputTechnology!=DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED,"self-test indirect output rejected");const auto idx=paths[0].sourceInfo.modeInfoIdx;require(idx<mc && modes[idx].infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE,"self-test mode unavailable");const auto &sm=modes[idx].sourceMode;require(sm.position.x==0 && sm.position.y==0 && sm.width>=640 && sm.height>=384,"self-test layout rejected");
    RECT rect{0,0,LONG(sm.width),LONG(sm.height)};TestWindow window;Reader reader(rect,paths[0].sourceInfo.adapterId,{{{70,70},{300,180}}});
    for(unsigned n=0;n<3;++n){const auto after=qpc();window.paint(n);const auto deadline=GetTickCount64()+8000;bool found=false;
        while(!found && GetTickCount64()<deadline){window.pump();Observation o;if(!reader.sample(GetCurrentProcessId(),o))continue;reader.emit(o);const auto &color=n==0?o.blue:n==1?o.green:o.red;found=o.present>=after && o.owned[0] && o.owned[1] && color[0] && color[1];if(found){const std::array<std::array<unsigned char,4>,3> expected{{{180,60,30,255},{60,180,20,255},{80,30,180,255}}};require(o.bgra[0]==expected[n] && o.bgra[1]==expected[n],"owned pixel evidence differs");}}
        require(found,"known physical color did not arrive");std::printf("{\"self_test_color\":%u,\"passed\":true}\n",n);std::fflush(stdout);
    }
    const auto after=qpc();window.paint(0);bool rejected=false;const auto deadline=GetTickCount64()+8000;
    while(!rejected && GetTickCount64()<deadline){window.pump();Observation o;if(!reader.sample(0,o))continue;rejected=o.present>=after && o.blue[0] && o.blue[1] && !o.owned[0] && !o.owned[1];if(rejected)require(o.bgra[0]==std::array<unsigned char,4>{} && o.bgra[1]==std::array<unsigned char,4>{},"unowned pixel exposed");}
    require(rejected,"wrong-owner observation accepted");std::printf("{\"completed\":true,\"passed\":true,\"self_test\":true,\"wrong_owner_rejected\":true,\"colors_verified\":3,\"gpu_copy_commands\":%llu,\"npu_tested\":false,\"physical_panel_observed\":false}\n",reader.copies);
}
int wmain(int argc,wchar_t **argv)try{
    require(nr::present::current_console_default(),"signed-in unlocked Default desktop required");require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=nullptr,"DPI awareness failed");
    if(argc==2 && !std::wcscmp(argv[1],L"--self-test")){self_test();return 0;}
    require(argc==5 && !std::wcscmp(argv[1],L"--observe"),"expected --observe <presenter-pid> <seconds> <stop-file>");wchar_t *end=nullptr;const auto pid=std::wcstoul(argv[2],&end,10);require(end!=argv[2] && !*end && pid,"PID rejected");const auto seconds=std::wcstoul(argv[3],&end,10);require(end!=argv[3] && !*end && seconds>=1 && seconds<=14400,"duration rejected");Peer peer(pid);const auto initial=nr::live::separated();require(initial.indirect.left==0 && initial.indirect.top==0 && initial.physical.left==1024 && initial.physical.top==0,"observer route rejected");Reader reader(initial.physical,initial.physical_source,{{{1222,166},{1312,234}}});const auto deadline=GetTickCount64()+seconds*1000ull;unsigned blue=0,green=0,red=0,rows=0;
    nr::present::DesktopAbsence absence;
    while(GetTickCount64()<deadline && GetFileAttributesW(argv[4])==INVALID_FILE_ATTRIBUTES){peer.verify();
        if(!nr::present::wait_console_default(absence,"observer",[&]{peer.verify();return GetFileAttributesW(argv[4])!=INVALID_FILE_ATTRIBUTES;}))break;
        require(nr::live::same(initial,nr::live::separated()),"observer topology changed");if(reader.lost)reader.reconnect();
        Observation o;if(reader.sample(pid,o)){peer.verify();reader.emit(o);++rows;for(size_t i=0;i<2;++i){blue+=o.owned[i] && o.blue[i];green+=o.owned[i] && o.green[i];red+=o.owned[i] && o.red[i];}}Sleep(100);}
    const bool passed=blue && green && red;std::printf("{\"completed\":true,\"passed\":%s,\"physical_output_observer\":true,\"rows\":%u,\"blue\":%u,\"green\":%u,\"red\":%u,\"gpu_copy_commands\":%llu,\"timeouts\":%llu,\"physical_panel_observed\":false,\"desktop_offloaded\":false,\"desktop_switches\":%u,\"desktop_away_ms\":%llu,\"duplication_reconnects\":%u}\n",json_bool(passed),rows,blue,green,red,reader.copies,reader.timeouts,absence.switches,absence.away_ms,reader.reconnects);return passed?0:1;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
