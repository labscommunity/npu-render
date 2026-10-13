// Bounded native D3D11 registration diagnostic; no display or topology changes.
#define NOMINMAX
#pragma warning(push)
#pragma warning(disable: 4201)
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <d3dkmthk.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#pragma warning(pop)
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <vector>
#include "contract.hpp"
#include "kernel_contract.hpp"

using Microsoft::WRL::ComPtr;
void check(HRESULT status, const char *operation) {
    std::printf("{\"operation\":\"%s\",\"status\":\"%08lx\"}\n", operation, ULONG(status));
    std::fflush(stdout);
    if (FAILED(status)) throw std::runtime_error(operation);
}
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
LUID select_adapter(WCHAR (&umd)[MAX_PATH]) {
    struct Handles {
        std::vector<D3DKMT_ADAPTERINFO> values;
        ~Handles() { for (const auto &a : values) if (a.hAdapter) { D3DKMT_CLOSEADAPTER close{a.hAdapter}; D3DKMTCloseAdapter(&close); } }
    } handles;
    D3DKMT_ENUMADAPTERS2 list{};
    require(D3DKMTEnumAdapters2(&list) == 0 && list.NumAdapters && list.NumAdapters <= 64, "adapter count");
    handles.values.resize(list.NumAdapters);
    list.pAdapters = handles.values.data();
    require(D3DKMTEnumAdapters2(&list) == 0, "adapter enumeration");
    UINT matches = 0;
    LUID selected{};
    for (const auto &a : handles.values) {
        nr::kernel_contract::AdapterInfo info{};
        std::memset(&info, 0, sizeof(info));
        D3DKMT_QUERYADAPTERINFO query{};
        query.hAdapter = a.hAdapter; query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
        query.pPrivateDriverData = &info; query.PrivateDriverDataSize = sizeof(info);
        const auto status = D3DKMTQueryAdapterInfo(&query);
        const nr::kernel_contract::AdapterInfo expected{};
        if (!status && info.signature == expected.signature && info.abi == expected.abi &&
            info.bytes == expected.bytes && info.ready == 0) {
            ++matches; selected = a.AdapterLuid;
            D3DKMT_UMDFILENAMEINFO name{};
            name.Version = KMTUMDVERSION_DX10;
            query.Type = KMTQAITYPE_UMDRIVERNAME;
            query.pPrivateDriverData = &name; query.PrivateDriverDataSize = sizeof(name);
            require(D3DKMTQueryAdapterInfo(&query) == 0 && std::wmemchr(name.UmdFileName, 0, MAX_PATH), "registered UMD path query");
            std::memcpy(umd, name.UmdFileName, sizeof(umd));
        }
    }
    require(matches == 1, "expected exactly one paired laboratory adapter");
    return selected;
}
void exercise(ID3D11Device *device, ID3D11DeviceContext *context, DXGI_FORMAT format) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64; desc.Height = 48; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target, staging;
    check(device->CreateTexture2D(&desc, nullptr, &target), "create_target");
    ComPtr<ID3D11RenderTargetView> view;
    check(device->CreateRenderTargetView(target.Get(), nullptr, &view), "create_view");
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&desc, nullptr, &staging), "create_staging");
    const FLOAT colors[3][4] = {{.2f, .4f, .6f, 1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}};
    const unsigned char values[3][4] = {{51, 102, 153, 255}, {255, 0, 0, 255}, {0, 255, 0, 255}};
    for (unsigned pass = 0; pass < 3; ++pass) {
        context->ClearRenderTargetView(view.Get(), colors[pass]);
        check(device->GetDeviceRemovedReason(), "clear_device_status");
        context->CopyResource(staging.Get(), target.Get());
        check(device->GetDeviceRemovedReason(), "copy_device_status");
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "map_staging");
        bool valid = mapped.pData && mapped.RowPitch >= desc.Width * 4;
        for (UINT y = 0; valid && y < desc.Height; ++y) {
            const auto row = static_cast<const unsigned char *>(mapped.pData) + size_t(y) * mapped.RowPitch;
            for (UINT x = 0; valid && x < desc.Width; ++x) {
                for (UINT c = 0; c < 4; ++c) {
                    const auto channel = format == DXGI_FORMAT_B8G8R8A8_UNORM && c != 1 && c != 3 ? 2 - c : c;
                    if (row[x * 4 + c] != values[pass][channel]) { valid = false; break; }
                }
            }
        }
        context->Unmap(staging.Get(), 0);
        check(device->GetDeviceRemovedReason(), "unmap_device_status");
        require(valid, "clear pixel mismatch");
        std::printf("{\"format\":%u,\"pass\":%u,\"verified_bytes\":12288}\n", UINT(format), pass);
    }
    context->Flush();
}

ComPtr<ID3DBlob> compile_shader(const char *source,const char *profile){
    ComPtr<ID3DBlob> code,errors;check(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors),"compile_shader");return code;
}
void draw_test(ID3D11Device *device,ID3D11DeviceContext *context){
    auto vsb=compile_shader("cbuffer C {float4 color;}; struct O {float4 p:SV_Position;float4 c:COLOR;}; O main(float4 p:POSITION){O o;o.p=p;o.c=color;return o;}","vs_4_0");
    auto psb=compile_shader("float4 main(float4 p:SV_Position,float4 c:COLOR):SV_Target{return c;}","ps_4_0");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    check(device->CreateVertexShader(vsb->GetBufferPointer(),vsb->GetBufferSize(),nullptr,&vs),"create_vs");check(device->CreatePixelShader(psb->GetBufferPointer(),psb->GetBufferSize(),nullptr,&ps),"create_ps");
    D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};ComPtr<ID3D11InputLayout> layout;
    check(device->CreateInputLayout(&element,1,vsb->GetBufferPointer(),vsb->GetBufferSize(),&layout),"create_layout");
    const float vertices[4][4]{{-1,1,.5f,1},{1,1,.5f,1},{1,-1,.5f,1},{-1,-1,.5f,1}};const uint16_t indices[]{0,1,2,2,3,0};float color[]{.5f,.25f,.125f,1};
    const auto buffer=[&](UINT bytes,UINT bind,const void *data){D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.BindFlags=bind;D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=data;ComPtr<ID3D11Buffer> result;check(device->CreateBuffer(&desc,&initial,&result),"create_buffer");return result;};
    auto vb=buffer(sizeof(vertices),D3D11_BIND_VERTEX_BUFFER,vertices),ib=buffer(sizeof(indices),D3D11_BIND_INDEX_BUFFER,indices),cb=buffer(sizeof(color),D3D11_BIND_CONSTANT_BUFFER,color);
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=64;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target,staging;check(device->CreateTexture2D(&td,nullptr,&target),"draw_target");ComPtr<ID3D11RenderTargetView> view;check(device->CreateRenderTargetView(target.Get(),nullptr,&view),"draw_view");td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&staging),"draw_staging");
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"draw_raster");D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthFunc=D3D11_COMPARISON_ALWAYS;ComPtr<ID3D11DepthStencilState> depth;check(device->CreateDepthStencilState(&dd,&depth),"draw_depth");D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=15;ComPtr<ID3D11BlendState> blend;check(device->CreateBlendState(&bd,&blend),"draw_blend");
    context->IASetInputLayout(layout.Get());const UINT stride=16,offset=0;ID3D11Buffer *vbs[]{vb.Get()},*cbs[]{cb.Get()};context->IASetVertexBuffers(0,1,vbs,&stride,&offset);context->IASetIndexBuffer(ib.Get(),DXGI_FORMAT_R16_UINT,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->VSSetConstantBuffers(0,1,cbs);context->RSSetState(raster.Get());D3D11_VIEWPORT viewport{0,0,64,64,0,1};context->RSSetViewports(1,&viewport);context->OMSetDepthStencilState(depth.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,~0u);ID3D11RenderTargetView *views[]{view.Get()};context->OMSetRenderTargets(1,views,nullptr);
    for(UINT frame=0;frame<2;++frame){
        if(frame){color[0]=.25f;color[1]=.125f;color[2]=.5f;context->UpdateSubresource(cb.Get(),0,nullptr,color,0,0);}
        context->DrawIndexed(6,0,0);check(device->GetDeviceRemovedReason(),"indexed_draw_status");context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"draw_readback");
        bool exact=mapped.pData && mapped.RowPitch>=256;const std::array<unsigned char,4> expected=frame?std::array<unsigned char,4>{128,32,64,255}:std::array<unsigned char,4>{32,64,128,255};
        for(UINT y=0;exact && y<64;++y)for(UINT x=0;exact && x<64;++x)exact=!std::memcmp(static_cast<unsigned char*>(mapped.pData)+size_t(y)*mapped.RowPitch+x*4,expected.data(),4);
        context->Unmap(staging.Get(),0);require(exact,"indexed draw pixel mismatch");
    }
    context->ClearState();context->Flush();check(device->GetDeviceRemovedReason(),"draw_cleanup_status");
}
int wmain(int argc,wchar_t **argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);HMODULE module=nullptr;
    try {
        require(argc==2 || (argc==3 && !wcscmp(argv[2],L"--reject-unleased")),"arguments");
        require(SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32),"system library search");require(!GetModuleHandleW(L"openvino.dll"),"unexpected runtime");
        WCHAR path[MAX_PATH]{};const auto luid=select_adapter(path);require(!_wcsicmp(path,argv[1]),"registered module differs from verified installed path");
        module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);require(module!=nullptr,"registered DLL load");
        auto configure=reinterpret_cast<BOOL(*)(const char*)>(GetProcAddress(module,"nr_fill_configure"));auto stats=reinterpret_cast<BOOL(*)(NrFillStats*)>(GetProcAddress(module,"nr_fill_stats"));require(configure && stats && !GetProcAddress(module,"D3DKMTCreateDevice"),"driver exports");
        ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");ComPtr<IDXGIAdapter> adapter;check(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)),"selected_adapter");
        require(!configure("CPU") && !configure("GPU"),"unexpected provider accepted");const bool configured=configure("NPU")!=FALSE;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL actual{},requested=D3D_FEATURE_LEVEL_10_0;
        const auto created=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,&requested,1,D3D11_SDK_VERSION,&device,&actual,&context);
        if(argc==3){require(!configured && FAILED(created),"unleased native adapter admitted");NrFillStats s{};require(stats(&s) && !s.executions && !s.live_allocations && !s.live_contexts,"unleased device activity");require(FreeLibrary(module),"unleased unload");module=nullptr;std::puts(R"json({"unleased_native_adapter_rejected":true,"normal_d3d11_runtime":true,"passed":true})json");return 0;}
        require(configured,"lease configuration");check(created,"normal_d3d11_device");require(actual==requested,"unexpected feature level");
        exercise(device.Get(),context.Get(),DXGI_FORMAT_B8G8R8A8_UNORM);exercise(device.Get(),context.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);draw_test(device.Get(),context.Get());
        context.Reset();device.Reset();adapter.Reset();factory.Reset();NrFillStats s{};require(stats(&s),"stats");
        require(s.executions==8 && s.remote_npu_executions==8 && s.draws==2 && !s.cpu_executions && !s.gpu_executions && !s.npu_executions,"provider accounting");require(!s.live_allocations && !s.live_contexts && !s.cleanup_errors && !s.rejected,"cleanup");require(!std::strcmp(s.last_execution_device,"NPU (local indexed)"),"execution identity");require(!GetModuleHandleW(L"openvino.dll"),"unexpected runtime loaded");require(FreeLibrary(module),"driver unload");module=nullptr;
        std::puts(R"json({"native_registered_component":true,"passed":true,"npu_local_executions":8,"clears":6,"draws":2,"cpu_executions":0,"gpu_executions":0,"registered_adapter_tested":true,"normal_d3d11_runtime":true,"allocation_callbacks_emulated":false,"desktop_offloaded":false})json");return 0;
    }catch(const std::exception &e){std::fprintf(stderr,"registered native probe: %s\n",e.what());if(module)FreeLibrary(module);return 1;}
}
