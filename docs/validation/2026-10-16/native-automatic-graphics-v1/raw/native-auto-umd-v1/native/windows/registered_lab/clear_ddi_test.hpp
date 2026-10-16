// Actual ClearRenderTargetView callbacks; only allocation/copy callbacks are
// emulated. Provider bytes, mip isolation and failure atomicity are inspected.
int test_provider_clears(const D3DDDI_DEVICECALLBACKS &kt,const D3D10DDI_CORELAYER_DEVICECALLBACKS &um,const char *backend){
    if(!std::strcmp(backend,"CPU") || !std::strcmp(backend,"NPU_LAB_FILE"))return 0;
    TEST(nr_fill_configure(backend));emulate_copies=true;render_failure=false;last_error=S_OK;
    alignas(Device) unsigned char store[sizeof(Device)]{};D3D10DDI_DEVICEFUNCS f{};DXGI_DDI_BASE_FUNCTIONS dxgi{};D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate=store;create.hRTDevice.handle=runtime_handle;create.pDeviceFuncs=&f;create.pUMCallbacks=&um;create.pKTCallbacks=&kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions=&dxgi;create.Interface=D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({},&create)==DXGI_STATUS_NO_REDIRECTION);const auto h=create.hDrvDevice;
    const auto before=counters.executions;unsigned checks=0;
    for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM})for(bool shared:{false,true}){
        D3D10DDI_MIPINFO mips[3]{};D3D10_DDIARG_SUBRESOURCE_UP initial[3]{};std::vector<uint8_t> pixels[3];
        const UINT levels=shared?1:3;
        for(UINT n=0;n<levels;++n){auto &m=mips[n];m.TexelWidth=65u>>n;m.TexelHeight=(std::max)(1u,7u>>n);m.TexelDepth=1;
            pixels[n].assign(size_t(m.TexelWidth*4+11)*m.TexelHeight,0x39);initial[n].pSysMem=pixels[n].data();initial[n].SysMemPitch=m.TexelWidth*4+11;}
        D3D10DDIARG_CREATERESOURCE args{};args.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;args.MipLevels=levels;args.ArraySize=args.SampleDesc.Count=1;
        args.pMipInfoList=mips;args.pInitialDataUP=initial;args.Format=format;args.Usage=D3D10_DDI_USAGE_DEFAULT;args.BindFlags=D3D10_DDI_BIND_RENDER_TARGET;
        args.MiscFlags=shared?D3D10_DDI_RESOURCE_MISC_SHARED:0;
        alignas(Resource) unsigned char rs[sizeof(Resource)]{};D3D10DDI_HRESOURCE r{};r.pDrvPrivate=rs;D3D10DDI_HRTRESOURCE rt{};
        rt.handle=shared?reinterpret_cast<HANDLE>(uintptr_t(0x9003)):nullptr;f.pfnCreateResource(h,&args,r,rt);TEST(last_error==S_OK);
        for(UINT n=0;n<levels;++n){
            alignas(View) unsigned char vs[sizeof(View)]{};D3D10DDI_HRENDERTARGETVIEW v{};v.pDrvPrivate=vs;
            D3D10DDIARG_CREATERENDERTARGETVIEW desc{};desc.hDrvResource=r;desc.ResourceDimension=args.ResourceDimension;desc.Format=format;desc.Tex2D.MipSlice=n;desc.Tex2D.ArraySize=1;
            f.pfnCreateRenderTargetView(h,&desc,v,{});TEST(last_error==S_OK);
            auto a=mip_allocation(resource(r),n);const auto original=memory[a->handle];std::vector<std::vector<unsigned char>> other;
            for(UINT k=0;k<levels;++k)other.push_back(memory[mip_allocation(resource(r),k)->handle]);
            FLOAT color[4]{.5f,.25f,.75f,1};
            // Rejected calls cannot alter the target or dispatch the provider.
            const auto execs=counters.executions;
            color[0]=std::numeric_limits<float>::infinity();f.pfnClearRenderTargetView(h,v,color);TEST(last_error==E_INVALIDARG && memory[a->handle]==original);
            color[0]=.5f;a->locked=true;f.pfnClearRenderTargetView(h,v,color);a->locked=false;TEST(last_error==E_INVALIDARG && memory[a->handle]==original);
            const auto own=a->context;a->context=std::make_shared<Context>(counters);f.pfnClearRenderTargetView(h,v,color);a->context=own;
            TEST(last_error==E_INVALIDARG && memory[a->handle]==original && counters.executions==execs);
            last_error=S_OK;f.pfnClearRenderTargetView(h,v,color);TEST(last_error==S_OK && pending_copies.empty());
            auto expected=original;const std::array<unsigned char,4> value=format==DXGI_FORMAT_B8G8R8A8_UNORM?std::array<unsigned char,4>{191,64,128,255}:std::array<unsigned char,4>{128,64,191,255};
            for(UINT y=0;y<a->surface.height;++y)for(UINT x=0;x<a->surface.width;++x)std::memcpy(expected.data()+size_t(y)*a->surface.row_pitch+x*4,value.data(),4);
            TEST(memory[a->handle]==expected && counters.executions==execs+1);
            for(UINT k=0;k<levels;++k)if(k!=n)TEST(memory[mip_allocation(resource(r),k)->handle]==other[k]);
            ++checks;f.pfnDestroyRenderTargetView(h,v);
        }
        f.pfnDestroyResource(h,r);TEST(!counters.live_allocations && resource_handles.empty());
    }
    f.pfnDestroyDevice(h);emulate_copies=false;
    TEST(checks==8 && counters.executions==before+checks && !counters.live_contexts && !counters.live_allocations && !counters.cleanup_errors && !contract_error);
    std::printf("{\"provider_clear_ddi\":true,\"clears\":%u,\"execution_device\":\"%s\",\"mip_isolation\":true,\"shared_resources\":true,\"invalid_requests_atomic\":true,\"displayed_frames\":0}\n",checks,backend);return 0;
}
