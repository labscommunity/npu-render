// Real resource/view/map/copy DDIs with runtime allocation shims. Each mip owns
// independent storage; checks include odd extents, pitch, isolation and lifetime.
int test_mips(const D3DDDI_DEVICECALLBACKS &kt,const D3D10DDI_CORELAYER_DEVICECALLBACKS &um) {
    emulate_copies=true;render_failure=false;last_error=S_OK;
    alignas(Device) unsigned char device_store[sizeof(Device)]{};
    D3D10DDI_DEVICEFUNCS f{};DXGI_DDI_BASE_FUNCTIONS dxgi{};D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate=device_store;create.hRTDevice.handle=runtime_handle;
    create.pDeviceFuncs=&f;create.pUMCallbacks=&um;create.pKTCallbacks=&kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions=&dxgi;create.Interface=D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({},&create)==DXGI_STATUS_NO_REDIRECTION);const auto h=create.hDrvDevice;
    const auto executions=counters.executions;
    for(const UINT format:{28u,87u,65u}) {
        const UINT bpp=format==65 ? 1 : 4;
        constexpr UINT levels=11;
        D3D10DDI_MIPINFO mips[levels]{};D3D10_DDIARG_SUBRESOURCE_UP initial[levels]{};
        std::vector<uint8_t> pixels[levels];
        for(UINT i=0;i<levels;++i) {
            auto &m=mips[i];m.TexelWidth=(std::max)(1u,1025u>>i);m.TexelHeight=(std::max)(1u,769u>>i);m.TexelDepth=1;
            const UINT pitch=m.TexelWidth*bpp+12;pixels[i].resize(size_t(pitch)*m.TexelHeight);
            for(size_t b=0;b<pixels[i].size();++b)pixels[i][b]=uint8_t(b*13+i*41);
            initial[i].pSysMem=pixels[i].data();initial[i].SysMemPitch=pitch;
        }
        alignas(Resource) unsigned char stores[3][sizeof(Resource)]{};
        D3D10DDI_HRESOURCE source{},stage{},bad{};source.pDrvPrivate=stores[0];stage.pDrvPrivate=stores[1];bad.pDrvPrivate=stores[2];
        D3D10DDIARG_CREATERESOURCE a{};a.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;a.Format=DXGI_FORMAT(format);
        a.MipLevels=levels;a.ArraySize=a.SampleDesc.Count=1;a.pMipInfoList=mips;a.pInitialDataUP=initial;
        a.Usage=D3D10_DDI_USAGE_DEFAULT;a.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE|(format==65 ? 0 : D3D10_DDI_BIND_RENDER_TARGET);
        f.pfnCreateResource(h,&a,source,{});TEST(last_error==S_OK && mip_count(resource(source))==levels);
        const auto allocated=counters.allocations;
        for(UINT invalid=0;invalid<3;++invalid) {
            if(invalid==0)++mips[4].TexelWidth;
            if(invalid==1)a.MipLevels=12;
            if(invalid==2)a.MiscFlags=D3D10_DDI_RESOURCE_MISC_SHARED;
            f.pfnCreateResource(h,&a,bad,{});TEST(last_error==E_INVALIDARG && counters.allocations==allocated);
            f.pfnDestroyResource(h,bad);last_error=S_OK;
            mips[4].TexelWidth=1025u>>4;a.MipLevels=levels;a.MiscFlags=0;
        }
        a.Usage=D3D10_DDI_USAGE_STAGING;a.BindFlags=0;a.MapFlags=D3D10_DDI_CPU_ACCESS_READ;a.pInitialDataUP=nullptr;
        f.pfnCreateResource(h,&a,stage,{});f.pfnResourceCopy(h,stage,source);TEST(last_error==S_OK);
        for(UINT sub=0;sub<levels;++sub) {
            D3D10DDI_MAPPED_SUBRESOURCE mapped{};f.pfnStagingResourceMap(h,stage,sub,D3D10_DDI_MAP_READ,0,&mapped);
            TEST(last_error==S_OK && mapped.pData && mapped.RowPitch>=mips[sub].TexelWidth*bpp);
            for(UINT y=0;y<mips[sub].TexelHeight;++y)
                TEST(!std::memcmp(static_cast<const uint8_t *>(mapped.pData)+size_t(y)*mapped.RowPitch,
                    pixels[sub].data()+size_t(y)*initial[sub].SysMemPitch,mips[sub].TexelWidth*bpp));
            f.pfnStagingResourceUnmap(h,stage,sub);TEST(last_error==S_OK);
        }
        const auto level0=read_pixels(*resource(source).allocation);
        std::fill(pixels[3].begin(),pixels[3].end(),uint8_t(73));
        f.pfnResourceUpdateSubresourceUP(h,source,3,nullptr,pixels[3].data(),initial[3].SysMemPitch,0);
        TEST(last_error==S_OK && read_pixels(*resource(source).allocation)==level0);
        const D3D10_DDI_BOX region{0,0,0,8,4,1};
        f.pfnResourceCopyRegion(h,source,4,0,0,0,source,3,&region);
        TEST(last_error==S_OK);
        const auto p4=read_pixels(*mip_allocation(resource(source),4));
        for(UINT y=0;y<4;++y)for(UINT x=0;x<8*bpp;++x)TEST(p4[size_t(y)*mips[4].TexelWidth*bpp+x]==73);
        f.pfnResourceUpdateSubresourceUP(h,source,levels,nullptr,pixels[0].data(),initial[0].SysMemPitch,0);
        TEST(last_error==E_INVALIDARG && read_pixels(*resource(source).allocation)==level0);last_error=S_OK;
        alignas(View) unsigned char rtv_store[sizeof(View)]{},srv_store[sizeof(View)]{};
        D3D10DDI_HRENDERTARGETVIEW rtv{};rtv.pDrvPrivate=rtv_store;
        D3D10DDIARG_CREATERENDERTARGETVIEW rv{};rv.hDrvResource=source;rv.ResourceDimension=a.ResourceDimension;
        rv.Format=a.Format;rv.Tex2D.MipSlice=5;rv.Tex2D.ArraySize=1;
        f.pfnCreateRenderTargetView(h,&rv,rtv,{});TEST(last_error==(format==65 ? E_INVALIDARG : S_OK));last_error=S_OK;
        D3D10DDI_HSHADERRESOURCEVIEW srv{};srv.pDrvPrivate=srv_store;
        D3D10DDIARG_CREATESHADERRESOURCEVIEW sv{};sv.hDrvResource=source;sv.ResourceDimension=a.ResourceDimension;
        sv.Format=a.Format;sv.Tex2D.MostDetailedMip=5;sv.Tex2D.MipLevels=sv.Tex2D.ArraySize=1;
        f.pfnCreateShaderResourceView(h,&sv,srv,{});TEST(last_error==S_OK);
        TEST(static_cast<View *>(srv.pDrvPrivate)->allocation==mip_allocation(resource(source),5));
        if(format!=65)TEST(static_cast<View *>(srv.pDrvPrivate)->allocation==static_cast<View *>(rtv.pDrvPrivate)->allocation);
        const std::weak_ptr<Allocation> retained=static_cast<View *>(srv.pDrvPrivate)->allocation;
        f.pfnDestroyResource(h,source);f.pfnDestroyResource(h,stage);TEST(!retained.expired() && counters.live_allocations==1);
        f.pfnDestroyRenderTargetView(h,rtv);TEST(!retained.expired());
        f.pfnDestroyShaderResourceView(h,srv);TEST(retained.expired() && !counters.live_allocations);
    }
    f.pfnDestroyDevice(h);emulate_copies=false;
    TEST(counters.executions==executions && !counters.live_contexts && !counters.live_allocations && !contract_error && !counters.cleanup_errors);
    std::puts("{\"kind\":\"mip-resource-ddi-shim\",\"levels\":11,\"formats\":3,\"odd_extents_and_pitches\":true,\"mip_isolation_and_lifetime\":true,\"npu_execution_tested\":false}");
    return 0;
}
