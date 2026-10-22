// A finite copy/presentation experiment. This does not render the desktop or
// infer that a frame came from the NPU; its controller must verify provenance.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <vector>
#include <string>
#include "session_gate.hpp"
#include "frame_wait.hpp"
using Microsoft::WRL::ComPtr;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
const char *phase="startup";
void check(HRESULT status){if(FAILED(status)){char message[192];std::snprintf(message,sizeof(message),"graphics call failed at %s: %08lx",phase,static_cast<unsigned long>(status));throw std::runtime_error(message);}}
struct FrameHeader {uint64_t magic;uint32_t version,header_bytes;uint64_t sequence;uint32_t width,height,format,row_bytes;uint64_t pixel_bytes,qpc;uint32_t producer_pid,reserved;};
static_assert(sizeof(FrameHeader)==64);
struct Frame {FrameHeader header{};std::vector<uint8_t> pixels;};
Frame read_frame(const wchar_t *path,const FrameHeader *previous=nullptr){
    const auto file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(file!=INVALID_HANDLE_VALUE,"frame open failed");
    try{
        LARGE_INTEGER size{};require(GetFileSizeEx(file,&size) && size.QuadPart>=64 && size.QuadPart<=64+4*4*1024*1024,"frame size rejected");
        Frame result;DWORD read=0;require(ReadFile(file,&result.header,64,&read,nullptr) && read==64,"frame header truncated");const auto &h=result.header;
        require(h.magic==0x31454d415246524eull && h.version==1 && h.header_bytes==64 && h.sequence && h.qpc && h.producer_pid && !h.reserved,"frame identity rejected");
        require(h.width && h.height && h.width<=8192 && h.height<=8192 && uint64_t(h.width)*h.height<=4*1024*1024 && (h.format==28 || h.format==87) && h.row_bytes==h.width*4 && h.pixel_bytes==uint64_t(h.row_bytes)*h.height && uint64_t(size.QuadPart)==64+h.pixel_bytes,"frame layout rejected");
        if(previous && h.sequence==previous->sequence){require(!std::memcmp(&h,previous,sizeof(h)),"live sequence header reused");CloseHandle(file);return result;}
        result.pixels.resize(size_t(h.pixel_bytes));require(ReadFile(file,result.pixels.data(),DWORD(result.pixels.size()),&read,nullptr) && read==result.pixels.size(),"frame pixels truncated");CloseHandle(file);return result;
    }catch(...){CloseHandle(file);throw;}
}
struct Window {
    HWND handle=nullptr;bool stopped=false,hotkey=false;
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto *self=reinterpret_cast<Window *>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(self && (message==WM_CLOSE || (message==WM_HOTKEY && w==1))){self->stopped=true;ShowWindow(hwnd,SW_HIDE);return 0;}
        if(message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN || message==WM_MBUTTONDOWN)return 0;
        return DefWindowProcW(hwnd,message,w,l);
    }
    explicit Window(const RECT &rect){
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"NpuRenderFiniteFramePresent";
        require(RegisterClassW(&cls)!=0,"window class registration failed");
        handle=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"NPU frame presentation probe",WS_POPUP,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,cls.hInstance,this);
        require(handle!=nullptr,"probe window creation failed");
        hotkey=RegisterHotKey(handle,1,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F12)!=0;
        if(!hotkey){DestroyWindow(handle);handle=nullptr;throw std::runtime_error("emergency hotkey unavailable");}
    }
    ~Window(){if(handle){if(hotkey)UnregisterHotKey(handle,1);ShowWindow(handle,SW_HIDE);DestroyWindow(handle);}}
    void messages(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT)stopped=true;TranslateMessage(&m);DispatchMessageW(&m);}}
};
}
int inspect_topology(){
    require(nr::present::current_console_default(),"signed-in unlocked console required for topology inspection");
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG status=ERROR_INSUFFICIENT_BUFFER;
    for(unsigned attempt=0;attempt<4 && status==ERROR_INSUFFICIENT_BUFFER;++attempt){
        UINT path_count=0,mode_count=0;status=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&path_count,&mode_count);
        require(status==ERROR_SUCCESS && path_count && path_count<=64 && mode_count<=256,"topology size unavailable");
        paths.resize(path_count);modes.resize(mode_count);
        status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&path_count,paths.data(),&mode_count,modes.data(),nullptr);
        if(status==ERROR_SUCCESS){paths.resize(path_count);modes.resize(mode_count);}
    }
    require(status==ERROR_SUCCESS,"topology query failed");
    std::printf("{\"topology_inspection\":true,\"pid\":%lu,\"paths\":%zu,\"modes\":%zu,\"display_changed\":false}\n",GetCurrentProcessId(),paths.size(),modes.size());
    for(const auto &path:paths){
        const auto index=path.sourceInfo.modeInfoIdx;require(index<modes.size() && modes[index].infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE,"source mode unavailable");
        const auto &source=modes[index].sourceMode;
        std::printf("{\"active_path\":true,\"source_low\":%lu,\"source_high\":%ld,\"source_id\":%u,\"source_width\":%u,\"source_height\":%u,\"source_x\":%ld,\"source_y\":%ld,\"target_low\":%lu,\"target_high\":%ld,\"target_id\":%u,\"technology\":%u,\"rotation\":%u,\"scaling\":%u,\"refresh_numerator\":%u,\"refresh_denominator\":%u}\n",
            path.sourceInfo.adapterId.LowPart,path.sourceInfo.adapterId.HighPart,path.sourceInfo.id,source.width,source.height,source.position.x,source.position.y,
            path.targetInfo.adapterId.LowPart,path.targetInfo.adapterId.HighPart,path.targetInfo.id,UINT(path.targetInfo.outputTechnology),UINT(path.targetInfo.rotation),UINT(path.targetInfo.scaling),path.targetInfo.refreshRate.Numerator,path.targetInfo.refreshRate.Denominator);
    }
    return 0;
}
#include "live_topology.hpp"
// Diagnostic observations are kept separate from Present acceptance. A media
// composition mode is recorded as reported, not interpreted as zero GPU work.
void observe_display(IDXGISwapChain1 *swapchain,IDXGISwapChainMedia *media,
                     uint64_t source_sequence,UINT submitted_count,HRESULT count_status){
    DXGI_FRAME_STATISTICS stats{};const auto stats_status=swapchain->GetFrameStatistics(&stats);
    DXGI_FRAME_STATISTICS_MEDIA detail{};const auto media_status=media?media->GetFrameStatisticsMedia(&detail):E_NOINTERFACE;
    LARGE_INTEGER observed{};QueryPerformanceCounter(&observed);
    std::printf("{\"display_observation\":true,\"source_sequence\":%llu,\"observed_qpc\":%lld,\"submitted_count_status\":%ld,\"submitted_present_count\":%u,\"statistics_status\":%ld,\"displayed_present_count\":%u,\"present_refresh_count\":%u,\"sync_refresh_count\":%u,\"sync_qpc\":%lld,\"media_status\":%ld,\"media_present_count\":%u,\"media_sync_qpc\":%lld,\"media_composition_mode\":%u,\"residual_gpu_work_measured\":false}\n",static_cast<unsigned long long>(source_sequence),observed.QuadPart,static_cast<long>(count_status),submitted_count,static_cast<long>(stats_status),stats.PresentCount,stats.PresentRefreshCount,stats.SyncRefreshCount,stats.SyncQPCTime.QuadPart,static_cast<long>(media_status),detail.PresentCount,detail.SyncQPCTime.QuadPart,UINT(detail.CompositionMode));std::fflush(stdout);
}
int live(const wchar_t *path,const wchar_t *duration,const wchar_t *producer){
    wchar_t *end=nullptr;const auto seconds=std::wcstoul(duration,&end,10);require(end!=duration && !*end && seconds>=1 && seconds<=14400,"live duration rejected");
    const auto expected_producer=std::wcstoul(producer,&end,10);require(end!=producer && !*end && expected_producer,"live producer rejected");
    nr::present::FrameWait frame_wait(path);
    auto frame=read_frame(path);const auto h=frame.header;auto last_header=h;require(h.producer_pid==expected_producer,"live producer mismatch");
    const auto initial=nr::live::separated();
    require(nr::present::current_console_default(),"signed-in unlocked Default console desktop required");
    require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=nullptr,"per-monitor thread DPI awareness unavailable");
    require(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=0,"DPI awareness readback mismatch");
    const auto monitor=MonitorFromRect(&initial.physical,MONITOR_DEFAULTTONULL);MONITORINFO mi{};mi.cbSize=sizeof(mi);require(monitor && GetMonitorInfoW(monitor,&mi) && EqualRect(&mi.rcMonitor,&initial.physical),"physical route monitor unavailable");
    std::printf("{\"physical_route_selected\":true,\"physical_x\":%ld,\"physical_y\":%ld,\"indirect_primary\":%s}\n",mi.rcMonitor.left,mi.rcMonitor.top,initial.indirect.left==0&&initial.indirect.top==0?"true":"false");std::fflush(stdout);
    phase="create-dxgi-factory";ComPtr<IDXGIFactory2> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;ComPtr<IDXGIOutput6> output;DXGI_ADAPTER_DESC1 adapter_desc{};DXGI_OUTPUT_DESC1 output_desc{};
    for(UINT i=0;!adapter;++i){ComPtr<IDXGIAdapter1> candidate;const auto status=factory->EnumAdapters1(i,&candidate);if(status==DXGI_ERROR_NOT_FOUND)break;check(status);DXGI_ADAPTER_DESC1 desc{};check(candidate->GetDesc1(&desc));if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
        for(UINT j=0;;++j){ComPtr<IDXGIOutput> original;const auto result=candidate->EnumOutputs(j,&original);if(result==DXGI_ERROR_NOT_FOUND)break;check(result);ComPtr<IDXGIOutput6> next;check(original.As(&next));DXGI_OUTPUT_DESC1 info{};check(next->GetDesc1(&info));if(info.Monitor==monitor && info.AttachedToDesktop){adapter=candidate;adapter_desc=desc;output=next;output_desc=info;break;}}
    }
    require(adapter && output,"physical output adapter not found");require(nr::live::equal(adapter_desc.AdapterLuid,initial.physical_source),"live physical adapter mismatch");require(output_desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 && output_desc.BitsPerColor==8,"probe requires an existing 8-bit SDR output");
    UINT composition_flags=0;const auto composition_status=output->CheckHardwareCompositionSupport(&composition_flags);
    phase="create-d3d-device";ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
    check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,&level,&context));
    Window window(mi.rcMonitor);DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=h.width;desc.Height=h.height;desc.Format=DXGI_FORMAT(h.format);desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.Scaling=DXGI_SCALING_STRETCH;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    phase="create-swapchain";ComPtr<IDXGISwapChain1> swapchain;check(factory->CreateSwapChainForHwnd(device.Get(),window.handle,&desc,nullptr,output.Get(),&swapchain));phase="window-association";check(factory->MakeWindowAssociation(window.handle,DXGI_MWA_NO_ALT_ENTER));
    ComPtr<IDXGISwapChainMedia> media;const auto media_interface_status=swapchain.As(&media);
    std::printf("{\"media_interface_status\":%ld}\n",static_cast<long>(media_interface_status));std::fflush(stdout);
    D3D11_TEXTURE2D_DESC texture{};texture.Width=h.width;texture.Height=h.height;texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT(h.format);texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;texture.BindFlags=0;
    phase="create-live-source";D3D11_SUBRESOURCE_DATA data{frame.pixels.data(),h.row_bytes,UINT(frame.pixels.size())};ComPtr<ID3D11Texture2D> source;check(device->CreateTexture2D(&texture,&data,&source));
    phase="get-backbuffer";ComPtr<ID3D11Texture2D> back;check(swapchain->GetBuffer(0,IID_PPV_ARGS(&back)));context->CopyResource(back.Get(),source.Get());
    phase="create-readback-staging";texture.Usage=D3D11_USAGE_STAGING;texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&texture,nullptr,&staging));context->CopyResource(staging.Get(),back.Get());phase="map-readback";D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));bool exact=mapped.pData && mapped.RowPitch>=h.row_bytes;
    if(exact)for(UINT y=0;y<h.height;++y)if(std::memcmp(static_cast<const uint8_t *>(mapped.pData)+size_t(y)*mapped.RowPitch,frame.pixels.data()+size_t(y)*h.row_bytes,h.row_bytes)){exact=false;break;}
    context->Unmap(staging.Get(),0);require(exact,"backbuffer roundtrip pixel mismatch");back.Reset();
    std::printf("{\"probe_ready\":true,\"pid\":%lu,\"adapter_luid_high\":%ld,\"adapter_luid_low\":%lu,\"vendor_id\":%u,\"device_id\":%u,\"source_width\":%u,\"source_height\":%u,\"output_width\":%ld,\"output_height\":%ld,\"source_sequence\":%llu,\"source_producer_pid\":%u,\"hardware_composition_query\":%ld,\"hardware_composition_flags\":%u,\"backbuffer_roundtrip_exact\":true,\"shader_dispatches\":0,\"source_uploads\":1,\"source_npu_provenance_verified_by_probe\":false}\n",GetCurrentProcessId(),adapter_desc.AdapterLuid.HighPart,adapter_desc.AdapterLuid.LowPart,adapter_desc.VendorId,adapter_desc.DeviceId,h.width,h.height,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,static_cast<unsigned long long>(h.sequence),h.producer_pid,static_cast<long>(composition_status),composition_flags);std::fflush(stdout);
    require(nr::present::current_console_default(),"interactive desktop changed before presentation");
    phase="live-present-loop";ShowWindow(window.handle,SW_SHOWNOACTIVATE);const auto start=GetTickCount64();uint64_t presents=0,uploads=1,readbacks=1,sequence=0,qpc=0,occluded=0;bool first=true;nr::present::DesktopAbsence absence;
    UINT submitted_count=0;HRESULT count_status=E_PENDING;ULONGLONG last_observation=0;
    LARGE_INTEGER frequency{};require(QueryPerformanceFrequency(&frequency)!=0,"QPC frequency unavailable");
    while(!window.stopped && GetTickCount64()-start<uint64_t(seconds)*1000){
        window.messages();if(window.stopped)break;
        // The saver, lock screen or a secure prompt takes the input desktop away and gives it back; presentation pauses meanwhile.
        if(!nr::present::wait_console_default(absence,"present",[&]{window.messages();return window.stopped || GetTickCount64()-start>=uint64_t(seconds)*1000;}))break;
        require(nr::live::same(initial,nr::live::separated()),"live topology changed");
        if(!first && GetTickCount64()-last_observation>=500){observe_display(swapchain.Get(),media.Get(),sequence,submitted_count,count_status);last_observation=GetTickCount64();}
        auto next=first?std::move(frame):read_frame(path,&last_header);const auto &n=next.header;
        require(n.producer_pid==expected_producer && n.width==h.width && n.height==h.height && n.format==h.format && n.pixel_bytes==h.pixel_bytes,"live frame identity changed");
        require(n.sequence>=sequence && n.qpc>=qpc,"live frame sequence regressed");
        if(!first && n.sequence==sequence){require(n.qpc==qpc,"live sequence reused");frame_wait.wait(250);continue;}
        require(first || n.qpc>qpc,"live timestamp did not advance");
        if(!first){context->UpdateSubresource(source.Get(),0,nullptr,next.pixels.data(),n.row_bytes,UINT(next.pixels.size()));++uploads;}
        check(swapchain->GetBuffer(0,IID_PPV_ARGS(&back)));if(!first)context->CopyResource(back.Get(),source.Get());
        // Each new frame is checked before presentation. This diagnostic copy
        // is counted explicitly and can be removed only after qualification.
        if(!first){context->CopyResource(staging.Get(),back.Get());check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));bool match=mapped.pData && mapped.RowPitch>=n.row_bytes;
            if(match)for(UINT y=0;y<n.height;++y)if(std::memcmp(static_cast<const uint8_t *>(mapped.pData)+size_t(y)*mapped.RowPitch,next.pixels.data()+size_t(y)*n.row_bytes,n.row_bytes)){match=false;break;}
            context->Unmap(staging.Get(),0);require(match,"live backbuffer pixel mismatch");++readbacks;}
        back.Reset();LARGE_INTEGER submitted{},accepted{};require(QueryPerformanceCounter(&submitted)!=0,"QPC unavailable");
        const auto status=swapchain->Present(1,0);check(status);
        if(status==DXGI_STATUS_OCCLUDED){++occluded;std::printf("{\"live_present_occluded\":true,\"source_sequence\":%llu,\"occluded_presents\":%llu}\n",static_cast<unsigned long long>(n.sequence),static_cast<unsigned long long>(occluded));std::fflush(stdout);sequence=n.sequence;qpc=n.qpc;last_header=n;first=false;continue;}
        require(status==S_OK,"live present not visible");require(QueryPerformanceCounter(&accepted)!=0,"QPC unavailable");++presents;
        std::printf("{\"live_frame_presented\":true,\"source_sequence\":%llu,\"source_qpc\":%llu,\"producer_pid\":%u,\"submitted_qpc\":%lld,\"accepted_qpc\":%lld,\"qpc_frequency\":%lld,\"backbuffer_exact\":true,\"pixel_bytes\":%llu,\"scanout_mode_verified\":false}\n",static_cast<unsigned long long>(n.sequence),static_cast<unsigned long long>(n.qpc),n.producer_pid,submitted.QuadPart,accepted.QuadPart,frequency.QuadPart,static_cast<unsigned long long>(n.pixel_bytes));std::fflush(stdout);
        count_status=swapchain->GetLastPresentCount(&submitted_count);
        observe_display(swapchain.Get(),media.Get(),n.sequence,submitted_count,count_status);last_observation=GetTickCount64();
        sequence=n.sequence;qpc=n.qpc;last_header=n;first=false;
    }
    std::printf("{\"frame_wait_summary\":true,\"notifications\":%llu,\"message_wakes\":%llu,\"timeout_wakes\":%llu,\"maximum_health_check_wait_ms\":250}\n",static_cast<unsigned long long>(frame_wait.notifications),static_cast<unsigned long long>(frame_wait.messages),static_cast<unsigned long long>(frame_wait.timeouts));
    ShowWindow(window.handle,SW_HIDE);std::printf("{\"completed\":true,\"live_bridge_tested\":true,\"submitted_presents\":%llu,\"source_uploads\":%llu,\"gpu_copy_commands\":%llu,\"readback_copies\":%llu,\"shader_dispatches\":0,\"elapsed_ms\":%llu,\"topology_preserved\":true,\"input_forwarding_tested\":false,\"scanout_mode_verified\":false,\"desktop_offloaded\":false,\"desktop_switches\":%u,\"desktop_away_ms\":%llu,\"longest_desktop_absence_ms\":%llu,\"occluded_presents\":%llu}\n",static_cast<unsigned long long>(presents),static_cast<unsigned long long>(uploads),static_cast<unsigned long long>(presents+readbacks),static_cast<unsigned long long>(readbacks),static_cast<unsigned long long>(GetTickCount64()-start),absence.switches,absence.away_ms,absence.longest_ms,static_cast<unsigned long long>(occluded));return 0;

}
struct Stimulus {
    HWND window=nullptr;unsigned step=0;bool stopped=false;
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto *self=reinterpret_cast<Stimulus *>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Stimulus *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(self && message==WM_CLOSE){self->stopped=true;return 0;}
        if(self && message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(hwnd,&paint);RECT rect{};GetClientRect(hwnd,&rect);
            const auto brush=CreateSolidBrush(self->step%2?RGB(20,180,60):RGB(180,30,80));FillRect(dc,&rect,brush);DeleteObject(brush);
            wchar_t label[80]{};std::swprintf(label,80,L"NPU desktop stimulus %u",self->step);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));DrawTextW(dc,label,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);EndPaint(hwnd,&paint);
            LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);std::printf("{\"stimulus_painted\":true,\"step\":%u,\"qpc\":%lld,\"content_produced_by\":\"GDI\"}\n",self->step,qpc.QuadPart);std::fflush(stdout);return 0;}
        return DefWindowProcW(hwnd,message,w,l);
    }
    explicit Stimulus(const RECT &area){WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"NpuRenderIndirectStimulus";require(RegisterClassW(&cls)!=0,"stimulus class failed");
        require(area.right-area.left>=640 && area.bottom-area.top>=384,"stimulus desktop too small");window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"NPU desktop composition stimulus",WS_POPUP,area.left+64,area.top+64,512,256,nullptr,nullptr,cls.hInstance,this);require(window!=nullptr,"stimulus window failed");}
    ~Stimulus(){if(window){ShowWindow(window,SW_HIDE);DestroyWindow(window);}}
};
int stimulus(const wchar_t *duration){
    wchar_t *end=nullptr;const auto seconds=std::wcstoul(duration,&end,10);require(end!=duration && !*end && seconds>=1 && seconds<=14400,"stimulus duration rejected");require(nr::present::current_console_default(),"stimulus requires Default console");
    require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=nullptr,"stimulus DPI unavailable");const auto route=nr::live::separated();Stimulus window(route.indirect);ShowWindow(window.window,SW_SHOWNOACTIVATE);UpdateWindow(window.window);const auto start=GetTickCount64();
    while(!window.stopped && GetTickCount64()-start<uint64_t(seconds)*1000){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)window.stopped=true;TranslateMessage(&message);DispatchMessageW(&message);}
        require(nr::present::current_console_default() && nr::live::same(route,nr::live::separated()),"stimulus desktop changed");const auto step=unsigned((GetTickCount64()-start)/15000);if(step!=window.step){window.step=step;InvalidateRect(window.window,nullptr,FALSE);UpdateWindow(window.window);}Sleep(100);}
    std::printf("{\"completed\":true,\"stimulus_only\":true,\"steps\":%u,\"desktop_offloaded\":false}\n",window.step+1);return 0;
}
#include "input_stimulus.hpp"
int wmain(int argc,wchar_t **argv)try{
    if(argc==2 && !std::wcscmp(argv[1],L"--input-contract"))return input_contract();
    if(argc==4 && !std::wcscmp(argv[1],L"--input-stimulus"))return input_stimulus(argv[2],argv[3]);
    if(argc==3 && !std::wcscmp(argv[1],L"--stimulus"))return stimulus(argv[2]);
    if(argc==4 && !std::wcscmp(argv[1],L"--read-pair")){
        const auto first=read_frame(argv[2]);const auto next=read_frame(argv[3],&first.header);
        require(next.header.producer_pid==first.header.producer_pid && next.header.sequence>=first.header.sequence && next.header.qpc>=first.header.qpc,"frame pair identity rejected");
        require(next.header.sequence==first.header.sequence || next.header.qpc>first.header.qpc,"frame pair timestamp rejected");
        std::printf("{\"frame_pair_valid\":true,\"new_pixel_bytes_read\":%zu,\"sequence_advanced\":%s}\n",next.pixels.size(),next.header.sequence>first.header.sequence?"true":"false");return 0;
    }
    if(argc==5 && !std::wcscmp(argv[1],L"--live"))return live(argv[2],argv[3],argv[4]);
    if(argc==2 && !std::wcscmp(argv[1],L"--inspect-topology"))return inspect_topology();
    if(argc==2 && !std::wcscmp(argv[1],L"--inspect-session")){
        const auto session=WTSGetActiveConsoleSessionId();const bool unlocked=nr::present::session_unlocked(session),matches=nr::present::console_identity_matches(session);
        std::printf("{\"session_inspection\":true,\"console_session\":%lu,\"signed_in_and_unlocked\":%s,\"controller_matches_console_user\":%s,\"ready_for_interactive_task\":%s,\"desktop_changed\":false}\n",session,unlocked?"true":"false",matches?"true":"false",(unlocked&&matches)?"true":"false");return 0;
    }
    require(argc==3,"expected <verified NRFRAME1 packet> <seconds 1..30>");wchar_t *end=nullptr;const auto seconds=std::wcstoul(argv[2],&end,10);require(end!=argv[2] && !*end && seconds>=1 && seconds<=30,"duration rejected");
    const auto frame=read_frame(argv[1]);const auto &h=frame.header;
    require(nr::present::current_console_default(),"signed-in unlocked Default console desktop required");
    require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=nullptr,"per-monitor thread DPI awareness unavailable");
    require(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=0,"DPI awareness readback mismatch");
    const auto monitor=MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);MONITORINFO mi{};mi.cbSize=sizeof(mi);require(monitor && GetMonitorInfoW(monitor,&mi) && (mi.dwFlags&MONITORINFOF_PRIMARY),"primary monitor unavailable");
    phase="create-dxgi-factory";ComPtr<IDXGIFactory2> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;ComPtr<IDXGIOutput6> output;DXGI_ADAPTER_DESC1 adapter_desc{};DXGI_OUTPUT_DESC1 output_desc{};
    for(UINT i=0;!adapter;++i){ComPtr<IDXGIAdapter1> candidate;const auto status=factory->EnumAdapters1(i,&candidate);if(status==DXGI_ERROR_NOT_FOUND)break;check(status);DXGI_ADAPTER_DESC1 desc{};check(candidate->GetDesc1(&desc));if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
        for(UINT j=0;;++j){ComPtr<IDXGIOutput> original;const auto result=candidate->EnumOutputs(j,&original);if(result==DXGI_ERROR_NOT_FOUND)break;check(result);ComPtr<IDXGIOutput6> next;check(original.As(&next));DXGI_OUTPUT_DESC1 info{};check(next->GetDesc1(&info));if(info.Monitor==monitor && info.AttachedToDesktop){adapter=candidate;adapter_desc=desc;output=next;output_desc=info;break;}}
    }
    require(adapter && output,"physical output adapter not found");require(output_desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 && output_desc.BitsPerColor==8,"probe requires an existing 8-bit SDR output");
    UINT composition_flags=0;const auto composition_status=output->CheckHardwareCompositionSupport(&composition_flags);
    phase="create-d3d-device";ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
    check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,&level,&context));
    Window window(mi.rcMonitor);DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=h.width;desc.Height=h.height;desc.Format=DXGI_FORMAT(h.format);desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.Scaling=DXGI_SCALING_STRETCH;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    phase="create-swapchain";ComPtr<IDXGISwapChain1> swapchain;check(factory->CreateSwapChainForHwnd(device.Get(),window.handle,&desc,nullptr,output.Get(),&swapchain));phase="window-association";check(factory->MakeWindowAssociation(window.handle,DXGI_MWA_NO_ALT_ENTER));
    D3D11_TEXTURE2D_DESC texture{};texture.Width=h.width;texture.Height=h.height;texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT(h.format);texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_IMMUTABLE;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    phase="create-immutable-source";D3D11_SUBRESOURCE_DATA data{frame.pixels.data(),h.row_bytes,UINT(frame.pixels.size())};ComPtr<ID3D11Texture2D> source;check(device->CreateTexture2D(&texture,&data,&source));
    phase="get-backbuffer";ComPtr<ID3D11Texture2D> back;check(swapchain->GetBuffer(0,IID_PPV_ARGS(&back)));context->CopyResource(back.Get(),source.Get());
    phase="create-readback-staging";texture.Usage=D3D11_USAGE_STAGING;texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&texture,nullptr,&staging));context->CopyResource(staging.Get(),back.Get());phase="map-readback";D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));bool exact=mapped.pData && mapped.RowPitch>=h.row_bytes;
    if(exact)for(UINT y=0;y<h.height;++y)if(std::memcmp(static_cast<const uint8_t *>(mapped.pData)+size_t(y)*mapped.RowPitch,frame.pixels.data()+size_t(y)*h.row_bytes,h.row_bytes)){exact=false;break;}
    context->Unmap(staging.Get(),0);require(exact,"backbuffer roundtrip pixel mismatch");staging.Reset();back.Reset();
    std::printf("{\"probe_ready\":true,\"pid\":%lu,\"adapter_luid_high\":%ld,\"adapter_luid_low\":%lu,\"vendor_id\":%u,\"device_id\":%u,\"source_width\":%u,\"source_height\":%u,\"output_width\":%ld,\"output_height\":%ld,\"source_sequence\":%llu,\"source_producer_pid\":%u,\"hardware_composition_query\":%ld,\"hardware_composition_flags\":%u,\"backbuffer_roundtrip_exact\":true,\"shader_dispatches\":0,\"source_uploads\":1,\"source_npu_provenance_verified_by_probe\":false}\n",GetCurrentProcessId(),adapter_desc.AdapterLuid.HighPart,adapter_desc.AdapterLuid.LowPart,adapter_desc.VendorId,adapter_desc.DeviceId,h.width,h.height,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,static_cast<unsigned long long>(h.sequence),h.producer_pid,static_cast<long>(composition_status),composition_flags);std::fflush(stdout);
    require(nr::present::current_console_default(),"interactive desktop changed before presentation");
    phase="present-loop";ShowWindow(window.handle,SW_SHOWNOACTIVATE);const auto start=GetTickCount64();uint64_t presents=0,stats_ok=0;UINT last_present_count=0;
    while(!window.stopped && GetTickCount64()-start<uint64_t(seconds)*1000){window.messages();if(window.stopped || !nr::present::current_console_default())break;
        check(swapchain->GetBuffer(0,IID_PPV_ARGS(&back)));context->CopyResource(back.Get(),source.Get());back.Reset();const auto status=swapchain->Present(1,0);check(status);require(status==S_OK,"presentation not accepted as visible");++presents;
        DXGI_FRAME_STATISTICS stats{};const auto stats_status=swapchain->GetFrameStatistics(&stats);if(SUCCEEDED(stats_status)){++stats_ok;last_present_count=stats.PresentCount;}
        if(presents==1 || !(presents%60)){std::printf("{\"submitted_presents\":%llu,\"stats_status\":%ld,\"present_count\":%u,\"scanout_mode_verified\":false}\n",static_cast<unsigned long long>(presents),static_cast<long>(stats_status),stats.PresentCount);std::fflush(stdout);}
    }
    ShowWindow(window.handle,SW_HIDE);std::printf("{\"completed\":true,\"submitted_presents\":%llu,\"gpu_copy_commands\":%llu,\"successful_frame_statistics\":%llu,\"last_present_count\":%u,\"elapsed_ms\":%llu,\"scanout_mode_verified\":false,\"desktop_offload_tested\":false}\n",static_cast<unsigned long long>(presents),static_cast<unsigned long long>(presents),static_cast<unsigned long long>(stats_ok),last_present_count,static_cast<unsigned long long>(GetTickCount64()-start));return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
