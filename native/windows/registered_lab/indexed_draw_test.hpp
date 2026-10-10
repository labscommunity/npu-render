// Real indexed-draw DDI with explicit runtime allocation shims. No OS adapter.
#include "point_texture_fixture.hpp"
#include <cfloat>
int test_indexed_draw(const D3DDDI_DEVICECALLBACKS &kt, const D3D10DDI_CORELAYER_DEVICECALLBACKS &um, const char *backend) {
    TEST(nr_fill_configure(backend)); emulate_copies = true; last_error = S_OK;
    const auto draws_before = counters.draws, executions_before = counters.executions;
    alignas(Device) unsigned char device_store[sizeof(Device)]{};
    D3D10DDI_DEVICEFUNCS f{}; DXGI_DDI_BASE_FUNCTIONS dxgi{}; D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate = device_store; create.hRTDevice.handle = runtime_handle;
    create.pDeviceFuncs = &f; create.pUMCallbacks = &um; create.pKTCallbacks = &kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions = &dxgi; create.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({}, &create) == DXGI_STATUS_NO_REDIRECTION); const auto h = create.hDrvDevice;
    constexpr UINT width = 64, height = 64;
    alignas(Resource) unsigned char resource_store[6][sizeof(Resource)]{};
    D3D10DDI_HRESOURCE resources[6]{}; for (unsigned i = 0; i < 6; ++i) resources[i].pDrvPrivate = resource_store[i];
    D3D10DDI_MIPINFO mip{}; mip.TexelWidth = width; mip.TexelHeight = height; mip.TexelDepth = 1;
    std::vector<uint8_t> background(width * height * 4, 53);
    D3D10_DDIARG_SUBRESOURCE_UP initial{}; initial.pSysMem = background.data(); initial.SysMemPitch = width * 4;
    D3D10DDIARG_CREATERESOURCE args{}; args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    args.MipLevels = args.ArraySize = args.SampleDesc.Count = 1; args.pMipInfoList = &mip;
    args.Format = DXGI_FORMAT_B8G8R8A8_UNORM; args.Usage = D3D10_DDI_USAGE_DEFAULT;
    args.BindFlags = D3D10_DDI_BIND_RENDER_TARGET; args.pInitialDataUP = &initial;
    f.pfnCreateResource(h, &args, resources[0], {}); TEST(last_error == S_OK);
    const auto target_index = resource(resources[0]).allocation->handle;
    const float positions[5][4]{{0,0,0,0},{-1,1,.5f,1},{1,1,.5f,1},{1,-1,.5f,1},{-1,-1,.5f,1}};
    const uint16_t indices[]{9999,9999,0,1,2,2,3,0}; float color[4]{.5f,.25f,.125f,1};
    auto buffer = [&](unsigned i, UINT bind, UINT bytes, const void *data) {
        D3D10DDI_MIPINFO m{}; m.TexelWidth = bytes; m.TexelHeight = m.TexelDepth = 1;
        D3D10_DDIARG_SUBRESOURCE_UP up{}; up.pSysMem = const_cast<void *>(data);
        D3D10DDIARG_CREATERESOURCE a{}; a.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
        a.MipLevels = a.ArraySize = a.SampleDesc.Count = 1; a.pMipInfoList = &m;
        a.Usage = D3D10_DDI_USAGE_DEFAULT; a.BindFlags = bind; a.pInitialDataUP = &up;
        f.pfnCreateResource(h, &a, resources[i], {}); return last_error == S_OK;
    };
    TEST(buffer(1, D3D10_DDI_BIND_VERTEX_BUFFER, sizeof(positions), positions));
    TEST(buffer(2, D3D10_DDI_BIND_INDEX_BUFFER, sizeof(indices), indices));
    TEST(buffer(3, D3D10_DDI_BIND_CONSTANT_BUFFER, sizeof(color), color));
    alignas(View) unsigned char view_store[sizeof(View)]{}; D3D10DDI_HRENDERTARGETVIEW view{}; view.pDrvPrivate = view_store;
    D3D10DDIARG_CREATERENDERTARGETVIEW target{}; target.hDrvResource = resources[0];
    target.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D; target.Format = args.Format; target.Tex2D.ArraySize = 1;
    f.pfnCreateRenderTargetView(h, &target, view, {}); f.pfnSetRenderTargets(h, &view, 1, 0, {});
    const auto vw = shader_tokens("cbuffer C {float4 color;}; struct O {float4 p:SV_Position;float4 c:COLOR;}; O main(float4 p:POSITION){O o;o.p=p;o.c=color;return o;}", "vs_4_0");
    const auto pw = shader_tokens("float4 main(float4 p:SV_Position,float4 c:COLOR):SV_Target{return c;}", "ps_4_0");
    alignas(Shader) unsigned char shader_store[2][sizeof(Shader)]{}; D3D10DDI_HSHADER shaders[2]{};
    for (unsigned i = 0; i < 2; ++i) shaders[i].pDrvPrivate = shader_store[i];
    f.pfnCreateVertexShader(h, vw.data(), shaders[0], {}, nullptr); f.pfnCreatePixelShader(h, pw.data(), shaders[1], {}, nullptr);
    f.pfnVsSetShader(h, shaders[0]); f.pfnPsSetShader(h, shaders[1]);
    f.pfnDestroyShader(h, shaders[0]); f.pfnDestroyShader(h, shaders[1]); // Bound programs retain their token streams.
    D3D10DDIARG_INPUT_ELEMENT_DESC element{}; element.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    D3D10DDIARG_CREATEELEMENTLAYOUT layout_args{}; layout_args.NumElements = 1; layout_args.pVertexElements = &element;
    alignas(ElementLayout) unsigned char layout_store[sizeof(ElementLayout)]{}; D3D10DDI_HELEMENTLAYOUT layout{}; layout.pDrvPrivate = layout_store;
    f.pfnCreateElementLayout(h, &layout_args, layout, {}); f.pfnIaSetInputLayout(h, layout); f.pfnDestroyElementLayout(h, layout);
    const UINT stride = 16, offset = 0; f.pfnIaSetVertexBuffers(h, 0, 1, &resources[1], &stride, &offset);
    f.pfnIaSetIndexBuffer(h, resources[2], DXGI_FORMAT_R16_UINT, 2);
    f.pfnVsSetConstantBuffers(h, 0, 1, &resources[3]);
    f.pfnIaSetTopology(h, D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D10_DDI_VIEWPORT viewport{}; viewport.Width = width; viewport.Height = height; viewport.MaxDepth = 1;
    f.pfnSetViewports(h, 1, 0, &viewport);
    D3D10_DDI_RASTERIZER_DESC raster{}; raster.FillMode = D3D10_DDI_FILL_SOLID; raster.CullMode = D3D10_DDI_CULL_NONE;
    raster.DepthClipEnable = TRUE; raster.ScissorEnable = TRUE; D3D10DDI_HRASTERIZERSTATE rs{}; rs.pDrvPrivate = &raster;
    f.pfnSetRasterizerState(h, rs);
    D3D10_DDI_RECT scissor{0,0,width,height}; f.pfnSetScissorRects(h, 1, 0, &scissor);
    D3D10_DDI_BLEND_DESC blend{}; blend.RenderTargetWriteMask[0] = 15; D3D10DDI_HBLENDSTATE bs{}; bs.pDrvPrivate = &blend;
    f.pfnSetBlendState(h, bs, nullptr, ~0u); TEST(last_error == S_OK);
    for (unsigned frame = 0; frame < 2; ++frame) {
        if (frame) {
            color[0] = .25f; color[1] = .125f; color[2] = .5f;
            f.pfnDefaultConstantBufferUpdateSubresourceUP(h, resources[3], 0, nullptr, color, 0, 0);
        }
        f.pfnDrawIndexed(h, 6, 1, 1); TEST(last_error == S_OK && counters.draws == draws_before + frame + 1);
        for (size_t i = 0; i < width * height; ++i) for (unsigned c = 0; c < 4; ++c)
            TEST(memory[target_index][i * 4 + c] == uint8_t(std::nearbyint(double(color[c < 3 ? 2 - c : c]) * 255)));
        const auto before = memory[target_index]; const auto draws = counters.draws;
        f.pfnDrawIndexed(h, 6, 1, -1); TEST(last_error == E_INVALIDARG && memory[target_index] == before && counters.draws == draws);
        last_error = S_OK; f.pfnDrawIndexed(h, 6, UINT_MAX, 1);
        TEST(last_error == E_INVALIDARG && memory[target_index] == before && counters.draws == draws); last_error = S_OK;
    }
    // Exercise the textured specialization through actual binding and DrawIndexed
    // DDIs, including nonzero first/base offsets and failed-state preservation.
    const auto texture_vs=shader_tokens("cbuffer C {float4 color;};struct O {float4 p:SV_Position;float4 c:COLOR0;float2 uv:TEXCOORD0;};O main(float4 p:POSITION,float2 uv:TEXCOORD0){O o;o.p=p;o.c=color;o.uv=uv;return o;}","vs_4_0");
    f.pfnCreateVertexShader(h,texture_vs.data(),shaders[0],{},nullptr);
    f.pfnCreatePixelShader(h,nr::registered_lab::test_fixture::point_texture_pixel_words,shaders[1],{},nullptr);
    f.pfnVsSetShader(h,shaders[0]);f.pfnPsSetShader(h,shaders[1]);
    f.pfnDestroyShader(h,shaders[0]);f.pfnDestroyShader(h,shaders[1]);
    std::fill(std::begin(color),std::end(color),1.f);
    f.pfnDefaultConstantBufferUpdateSubresourceUP(h,resources[3],0,nullptr,color,0,0);
    constexpr UINT sw=96,sh=80;
    std::vector<uint8_t> source(size_t(sw)*sh*4);
    for(size_t i=0;i<source.size();++i) source[i]=uint8_t((i*17+i/4*11)&255);
    mip.TexelWidth=sw;mip.TexelHeight=sh;initial.pSysMem=source.data();initial.SysMemPitch=sw*4;
    args.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;f.pfnCreateResource(h,&args,resources[4],{});
    // The DDI owns construction/destruction in runtime-provided raw storage.
    // An automatic View here would release its shared allocation a second time.
    alignas(View) unsigned char source_view_store[sizeof(View)]{};
    D3D10DDI_HSHADERRESOURCEVIEW srv{};srv.pDrvPrivate=source_view_store;
    D3D10DDIARG_CREATESHADERRESOURCEVIEW sva{};sva.hDrvResource=resources[4];sva.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
    sva.Format=args.Format;sva.Tex2D.ArraySize=sva.Tex2D.MipLevels=1;f.pfnCreateShaderResourceView(h,&sva,srv,{});
    f.pfnPsSetShaderResources(h,0,1,&srv);
    float textured_positions[5][6]{};
    const float uv[5][2]{{0,0},{32.f/sw,16.f/sh},{1,16.f/sh},{1,1},{32.f/sw,1}};
    for(size_t i=0;i<5;++i){std::memcpy(textured_positions[i],positions[i],16);std::memcpy(textured_positions[i]+4,uv[i],8);}
    TEST(buffer(5,D3D10_DDI_BIND_VERTEX_BUFFER,sizeof(textured_positions),textured_positions));
    D3D10DDIARG_INPUT_ELEMENT_DESC elements[2]{};elements[0].Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    elements[1].Format=DXGI_FORMAT_R32G32_FLOAT;elements[1].AlignedByteOffset=16;elements[1].InputRegister=1;
    layout_args.NumElements=2;layout_args.pVertexElements=elements;
    f.pfnCreateElementLayout(h,&layout_args,layout,{});f.pfnIaSetInputLayout(h,layout);f.pfnDestroyElementLayout(h,layout);
    const UINT texture_stride=24;f.pfnIaSetVertexBuffers(h,0,1,&resources[5],&texture_stride,&offset);
    D3D10_DDI_SAMPLER_DESC sampler{};sampler.Filter=D3D10_DDI_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D10_DDI_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=FLT_MAX;
    D3D10DDI_HSAMPLER hs{};hs.pDrvPrivate=&sampler;f.pfnPsSetSamplers(h,0,1,&hs);TEST(last_error==S_OK);
    for(unsigned frame=0;frame<2;++frame){
        if(frame){for(auto &c:source)c=uint8_t(c+43);f.pfnResourceUpdateSubresourceUP(h,resources[4],0,nullptr,source.data(),sw*4,0);}
        f.pfnDrawIndexed(h,6,1,1);TEST(last_error==S_OK && counters.draws==draws_before+3+frame);
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)for(UINT c=0;c<4;++c)
            TEST(memory[target_index][(size_t(y)*width+x)*4+c]==(c==3?uint8_t(255):source[(size_t(y+16)*sw+x+32)*4+c]));
        const auto before=memory[target_index];const auto draws=counters.draws;
        sampler.Filter=D3D10_DDI_FILTER_MIN_MAG_MIP_LINEAR;f.pfnPsSetSamplers(h,0,1,&hs);f.pfnDrawIndexed(h,6,1,1);
        TEST(last_error==E_INVALIDARG && memory[target_index]==before && counters.draws==draws);last_error=S_OK;
        sampler.Filter=D3D10_DDI_FILTER_MIN_MAG_MIP_POINT;f.pfnPsSetSamplers(h,0,1,&hs);
    }
    f.pfnDestroyShaderResourceView(h,srv);
    f.pfnDestroyRenderTargetView(h, view);
    for (const auto r : resources) f.pfnDestroyResource(h, r);
    f.pfnDestroyDevice(h);
    TEST(counters.executions == executions_before + 4 && !counters.live_contexts && !counters.live_allocations && !counters.cleanup_errors && !contract_error);
    std::printf("{\"kind\":\"indexed-ddi-runtime-shim\",\"requested_backend\":\"%s\",\"execution_device\":\"%s\",\"verified_draws\":4,\"verified_components\":65536,\"textured_draws\":2,\"base_vertex_and_start_index\":true,\"invalid_ranges_and_sampler_rejected\":true,\"registered_adapter_tested\":false,\"desktop_offload_tested\":false}\n", backend, counters.last_execution_device);
    return 0;
}
