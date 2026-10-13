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
using Microsoft::WRL::ComPtr;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void check(HRESULT status){if(FAILED(status)){char message[64];std::snprintf(message,sizeof(message),"graphics call failed: %08lx",static_cast<unsigned long>(status));throw std::runtime_error(message);}}
struct FrameHeader {uint64_t magic;uint32_t version,header_bytes;uint64_t sequence;uint32_t width,height,format,row_bytes;uint64_t pixel_bytes,qpc;uint32_t producer_pid,reserved;};
static_assert(sizeof(FrameHeader)==64);
struct Frame {FrameHeader header{};std::vector<uint8_t> pixels;};
Frame read_frame(const wchar_t *path){
    const auto file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(file!=INVALID_HANDLE_VALUE,"frame open failed");
    try{
        LARGE_INTEGER size{};require(GetFileSizeEx(file,&size) && size.QuadPart>=64 && size.QuadPart<=64+4*4*1024*1024,"frame size rejected");
        Frame result;DWORD read=0;require(ReadFile(file,&result.header,64,&read,nullptr) && read==64,"frame header truncated");const auto &h=result.header;
        require(h.magic==0x31454d415246524eull && h.version==1 && h.header_bytes==64 && h.sequence && h.qpc && h.producer_pid && !h.reserved,"frame identity rejected");
        require(h.width && h.height && h.width<=8192 && h.height<=8192 && uint64_t(h.width)*h.height<=4*1024*1024 && (h.format==28 || h.format==87) && h.row_bytes==h.width*4 && h.pixel_bytes==uint64_t(h.row_bytes)*h.height && uint64_t(size.QuadPart)==64+h.pixel_bytes,"frame layout rejected");
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
int wmain(int argc,wchar_t **argv)try{
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
    ComPtr<IDXGIFactory2> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;ComPtr<IDXGIOutput6> output;DXGI_ADAPTER_DESC1 adapter_desc{};DXGI_OUTPUT_DESC1 output_desc{};
    for(UINT i=0;!adapter;++i){ComPtr<IDXGIAdapter1> candidate;const auto status=factory->EnumAdapters1(i,&candidate);if(status==DXGI_ERROR_NOT_FOUND)break;check(status);DXGI_ADAPTER_DESC1 desc{};check(candidate->GetDesc1(&desc));if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
        for(UINT j=0;;++j){ComPtr<IDXGIOutput> original;const auto result=candidate->EnumOutputs(j,&original);if(result==DXGI_ERROR_NOT_FOUND)break;check(result);ComPtr<IDXGIOutput6> next;check(original.As(&next));DXGI_OUTPUT_DESC1 info{};check(next->GetDesc1(&info));if(info.Monitor==monitor && info.AttachedToDesktop){adapter=candidate;adapter_desc=desc;output=next;output_desc=info;break;}}
    }
    require(adapter && output,"physical output adapter not found");require(output_desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 && output_desc.BitsPerColor==8,"probe requires an existing 8-bit SDR output");
    UINT composition_flags=0;const auto composition_status=output->CheckHardwareCompositionSupport(&composition_flags);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
    check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,&level,&context));
    Window window(mi.rcMonitor);DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=h.width;desc.Height=h.height;desc.Format=DXGI_FORMAT(h.format);desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.Scaling=DXGI_SCALING_STRETCH;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> swapchain;check(factory->CreateSwapChainForHwnd(device.Get(),window.handle,&desc,nullptr,output.Get(),&swapchain));check(factory->MakeWindowAssociation(window.handle,DXGI_MWA_NO_ALT_ENTER));
    D3D11_TEXTURE2D_DESC texture{};texture.Width=h.width;texture.Height=h.height;texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT(h.format);texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA data{frame.pixels.data(),h.row_bytes,UINT(frame.pixels.size())};ComPtr<ID3D11Texture2D> source;check(device->CreateTexture2D(&texture,&data,&source));
    ComPtr<ID3D11Texture2D> back;check(swapchain->GetBuffer(0,IID_PPV_ARGS(&back)));context->CopyResource(back.Get(),source.Get());
    texture.Usage=D3D11_USAGE_STAGING;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&texture,nullptr,&staging));context->CopyResource(staging.Get(),back.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));bool exact=mapped.pData && mapped.RowPitch>=h.row_bytes;
    if(exact)for(UINT y=0;y<h.height;++y)if(std::memcmp(static_cast<const uint8_t *>(mapped.pData)+size_t(y)*mapped.RowPitch,frame.pixels.data()+size_t(y)*h.row_bytes,h.row_bytes)){exact=false;break;}
    context->Unmap(staging.Get(),0);require(exact,"backbuffer roundtrip pixel mismatch");staging.Reset();back.Reset();
    std::printf("{\"probe_ready\":true,\"pid\":%lu,\"adapter_luid_high\":%ld,\"adapter_luid_low\":%lu,\"vendor_id\":%u,\"device_id\":%u,\"source_width\":%u,\"source_height\":%u,\"output_width\":%ld,\"output_height\":%ld,\"source_sequence\":%llu,\"source_producer_pid\":%u,\"hardware_composition_query\":%ld,\"hardware_composition_flags\":%u,\"backbuffer_roundtrip_exact\":true,\"shader_dispatches\":0,\"source_uploads\":1,\"source_npu_provenance_verified_by_probe\":false}\n",GetCurrentProcessId(),adapter_desc.AdapterLuid.HighPart,adapter_desc.AdapterLuid.LowPart,adapter_desc.VendorId,adapter_desc.DeviceId,h.width,h.height,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,static_cast<unsigned long long>(h.sequence),h.producer_pid,static_cast<long>(composition_status),composition_flags);std::fflush(stdout);
    require(nr::present::current_console_default(),"interactive desktop changed before presentation");
    ShowWindow(window.handle,SW_SHOWNOACTIVATE);const auto start=GetTickCount64();uint64_t presents=0,stats_ok=0;UINT last_present_count=0;
    while(!window.stopped && GetTickCount64()-start<uint64_t(seconds)*1000){window.messages();if(window.stopped || !nr::present::current_console_default())break;
        check(swapchain->GetBuffer(0,IID_PPV_ARGS(&back)));context->CopyResource(back.Get(),source.Get());back.Reset();const auto status=swapchain->Present(1,0);check(status);require(status==S_OK,"presentation not accepted as visible");++presents;
        DXGI_FRAME_STATISTICS stats{};const auto stats_status=swapchain->GetFrameStatistics(&stats);if(SUCCEEDED(stats_status)){++stats_ok;last_present_count=stats.PresentCount;}
        if(presents==1 || !(presents%60)){std::printf("{\"submitted_presents\":%llu,\"stats_status\":%ld,\"present_count\":%u,\"scanout_mode_verified\":false}\n",static_cast<unsigned long long>(presents),static_cast<long>(stats_status),stats.PresentCount);std::fflush(stdout);}
    }
    ShowWindow(window.handle,SW_HIDE);std::printf("{\"completed\":true,\"submitted_presents\":%llu,\"gpu_copy_commands\":%llu,\"successful_frame_statistics\":%llu,\"last_present_count\":%u,\"elapsed_ms\":%llu,\"scanout_mode_verified\":false,\"desktop_offload_tested\":false}\n",static_cast<unsigned long long>(presents),static_cast<unsigned long long>(presents),static_cast<unsigned long long>(stats_ok),last_present_count,static_cast<unsigned long long>(GetTickCount64()-start));return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
