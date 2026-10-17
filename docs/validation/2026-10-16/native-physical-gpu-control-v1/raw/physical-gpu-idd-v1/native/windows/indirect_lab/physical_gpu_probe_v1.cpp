// Explicit hardware-control qualification; never a desktop or NPU proof.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cwchar>
#include "physical_gpu_choice_v1.hpp"
using Microsoft::WRL::ComPtr;
void require(bool yes,const char *message){if(!yes)throw std::runtime_error(message);}
void check(HRESULT h){if(FAILED(h)){char text[80];std::snprintf(text,sizeof(text),"hardware GPU control HRESULT %08lx",static_cast<unsigned long>(h));throw std::runtime_error(text);}}
void contract(){
    using nr::control::Adapter;const Adapter physical{0x8086,0xb080,0,17,0},software{0x8086,0xb080,2,18,0},other{0x1414,0x80,0,19,0};
    require(nr::control::choose({other,software,physical},0x8086,0xb080)==2,"hardware selection failed");
    require(nr::control::choose({physical,other},0x8086,0xb080)==0,"hardware order changed");unsigned checks=2;
    const std::vector<std::vector<Adapter>> rejected{{},{software},{other},{physical,physical},{{0x8086,0xb080,0,0,0}},{{0x8086,0xb080,1,20,0}},std::vector<Adapter>(65,physical)};
    for(const auto &a:rejected){bool failed=false;try{(void)nr::control::choose(a,0x8086,0xb080);}catch(const std::exception&){failed=true;}require(failed,"invalid selection accepted");++checks;}
    for(const auto ids:std::array<std::array<unsigned,2>,2>{{{0,0xb080},{0x8086,0}}}){bool failed=false;try{(void)nr::control::choose({physical},ids[0],ids[1]);}catch(const std::exception&){failed=true;}require(failed,"empty target accepted");++checks;}
    std::printf("{\"completed\":true,\"passed\":true,\"selection_contract_checks\":%u,\"gpu_commands\":0,\"npu_commands\":0}\n",checks);
}
unsigned argument(const wchar_t *text){wchar_t *end=nullptr;const auto v=std::wcstoul(text,&end,0);require(end!=text && !*end && v>0 && v<=0xffff,"PCI target rejected");return unsigned(v);}
int wmain(int argc,wchar_t **argv)try{
    if(argc==2 && !std::wcscmp(argv[1],L"--contract")){contract();return 0;}
    require(argc==4 && (!std::wcscmp(argv[1],L"--inspect") || !std::wcscmp(argv[1],L"--clear")),"expected --inspect/--clear <vendor-id> <device-id>");
    const auto vendor=argument(argv[2]),device_id=argument(argv[3]);const bool clear=!std::wcscmp(argv[1],L"--clear");
    ComPtr<IDXGIFactory1> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));std::vector<nr::control::Adapter> descriptions;std::vector<ComPtr<IDXGIAdapter1>> adapters;
    for(UINT index=0;;++index){require(index<=64,"adapter enumeration bound exceeded");ComPtr<IDXGIAdapter1> adapter;auto h=factory->EnumAdapters1(index,&adapter);if(h==DXGI_ERROR_NOT_FOUND)break;check(h);DXGI_ADAPTER_DESC1 d{};check(adapter->GetDesc1(&d));descriptions.push_back({d.VendorId,d.DeviceId,UINT(d.Flags),d.AdapterLuid.LowPart,d.AdapterLuid.HighPart});adapters.push_back(adapter);}
    const auto selected=nr::control::choose(descriptions,vendor,device_id);const auto &chosen=descriptions[selected];D3D_FEATURE_LEVEL level{};unsigned verified=0;
    if(clear){
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};check(D3D11CreateDevice(adapters[selected].Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,&level,&context));
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> actual;check(device.As(&dxgi));check(dxgi->GetAdapter(&actual));DXGI_ADAPTER_DESC actual_desc{};check(actual->GetDesc(&actual_desc));require(actual_desc.AdapterLuid.LowPart==chosen.low && actual_desc.AdapterLuid.HighPart==chosen.high && actual_desc.VendorId==vendor && actual_desc.DeviceId==device_id,"device selected a different adapter");
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=128;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> view;check(device->CreateTexture2D(&td,nullptr,&target));check(device->CreateRenderTargetView(target.Get(),nullptr,&view));td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&staging));
        const std::array<std::array<float,4>,4> colors{{{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,1,1}}};
        for(const auto &color:colors){context->ClearRenderTargetView(view.Get(),color.data());context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));bool exact=mapped.pData && mapped.RowPitch>=512;
            const std::array<unsigned char,4> expected{static_cast<unsigned char>(color[2]*255),static_cast<unsigned char>(color[1]*255),static_cast<unsigned char>(color[0]*255),255};
            if(exact)for(unsigned y=0;y<128;++y){auto row=static_cast<const unsigned char*>(mapped.pData)+size_t(y)*mapped.RowPitch;for(unsigned x=0;x<128;++x)for(unsigned c=0;c<4;++c)exact=exact && row[x*4+c]==expected[c];}
            context->Unmap(staging.Get(),0);require(exact,"hardware clear image differs");++verified;
        }
    }
    DWORD session=0;require(ProcessIdToSessionId(GetCurrentProcessId(),&session)!=0,"session unavailable");
    std::printf("{\"completed\":true,\"passed\":true,\"hardware_gpu_control\":true,\"vendor\":%u,\"device\":%u,\"luid_low\":%u,\"luid_high\":%d,\"session\":%lu,\"feature_level\":%u,\"exact_clear_images\":%u,\"npu_commands\":0,\"desktop_changed\":false,\"tahoma_tested\":false}\n",vendor,device_id,chosen.low,chosen.high,session,UINT(level),verified);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
