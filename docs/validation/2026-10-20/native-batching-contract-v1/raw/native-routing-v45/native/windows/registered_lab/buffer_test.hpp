// Real UMD callbacks with allocation shims; not a live adapter or NPU test.
int test_buffers(const D3DDDI_DEVICECALLBACKS &kt, const D3D10DDI_CORELAYER_DEVICECALLBACKS &um) {
    render_failure = false; last_error = S_OK;
    alignas(Device) unsigned char storage[sizeof(Device)]{};
    D3D10DDI_DEVICEFUNCS f{}; DXGI_DDI_BASE_FUNCTIONS dxgi{};
    D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate = storage; create.hRTDevice.handle = runtime_handle;
    create.pDeviceFuncs = &f; create.pUMCallbacks = &um; create.pKTCallbacks = &kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions = &dxgi; create.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({}, &create) == DXGI_STATUS_NO_REDIRECTION);
    const auto h = create.hDrvDevice;
    for (const UINT bytes : {16u, 272u, 32768u, 32784u, 65536u}) {
        std::vector<unsigned char> input(bytes);
        for (UINT i = 0; i < bytes; ++i) input[i] = static_cast<unsigned char>(i * 37 + bytes / 16);
        D3D10DDI_MIPINFO mip{}; mip.TexelWidth = bytes; mip.TexelHeight = mip.TexelDepth = 1;
        D3D10_DDIARG_SUBRESOURCE_UP initial{}; initial.pSysMem = input.data();
        D3D10DDIARG_CREATERESOURCE a{};
        a.ResourceDimension = D3D10DDIRESOURCE_BUFFER; a.MipLevels = a.ArraySize = 1;
        a.Format = DXGI_FORMAT_UNKNOWN; a.SampleDesc.Count = 1; a.pMipInfoList = &mip;
        a.BindFlags = D3D10_DDI_BIND_CONSTANT_BUFFER; a.Usage = D3D10_DDI_USAGE_DEFAULT; a.pInitialDataUP = &initial;
        alignas(Resource) unsigned char target_store[sizeof(Resource)]{}, staging_store[sizeof(Resource)]{}, dynamic_store[sizeof(Resource)]{};
        D3D10DDI_HRESOURCE target{}, staging{}, dynamic{};
        target.pDrvPrivate = target_store; staging.pDrvPrivate = staging_store; dynamic.pDrvPrivate = dynamic_store;
        f.pfnCreateResource(h, &a, target, {});
        TEST(last_error == S_OK && resource(target).buffer_bytes == bytes);
        const auto index = resource(target).allocation->handle;
        TEST(std::equal(input.begin(), input.end(), memory[index].begin()));
        std::fill(input.begin(), input.end(), static_cast<unsigned char>(0xa7));
        TEST(memory[index][0] != 0xa7); // Initial data is owned by VidMm, not the caller.
        f.pfnDefaultConstantBufferUpdateSubresourceUP(h, target, 0, nullptr, input.data(), 0, 0);
        TEST(last_error == S_OK && std::equal(input.begin(), input.end(), memory[index].begin()));
        const auto expected = memory[index];
        const D3D10_DDI_BOX partial{0, 0, 0, 16, 1, 1};
        f.pfnResourceUpdateSubresourceUP(h, target, 0, &partial, input.data(), 0, 0);
        TEST(last_error == E_INVALIDARG && memory[index] == expected);
        lock_failure = true;
        f.pfnResourceUpdateSubresourceUP(h, target, 0, nullptr, input.data(), 0, 0);
        TEST(last_error == E_ACCESSDENIED && memory[index] == expected && !resource(target).allocation->locked);
        lock_failure = false; last_error = S_OK;
        a.Usage = D3D10_DDI_USAGE_STAGING; a.BindFlags = 0; a.MapFlags = D3D10_DDI_CPU_ACCESS_READ; a.pInitialDataUP = nullptr;
        f.pfnCreateResource(h, &a, staging, {});
        f.pfnResourceCopy(h, staging, target);
        D3D10DDI_MAPPED_SUBRESOURCE mapped{};
        f.pfnResourceMap(h, staging, 0, D3D10_DDI_MAP_READ, 0, &mapped);
        TEST(last_error == S_OK && mapped.pData && !std::memcmp(mapped.pData, input.data(), bytes));
        f.pfnResourceUnmap(h, staging, 0);
        f.pfnVsSetConstantBuffers(h, 0, 1, &target);
        TEST(last_error == S_OK && device(h).constant_buffers[0][0] == resource(target).allocation);
        D3D10DDI_HRESOURCE invalid[]{target, staging};
        f.pfnVsSetConstantBuffers(h, 1, 2, invalid);
        TEST(last_error == E_INVALIDARG && !device(h).constant_buffers[0][1] && !device(h).constant_buffers[0][2]);
        last_error = S_OK;
        a.Usage = D3D10_DDI_USAGE_DYNAMIC; a.BindFlags = D3D10_DDI_BIND_CONSTANT_BUFFER; a.MapFlags = D3D10_DDI_CPU_ACCESS_WRITE;
        f.pfnCreateResource(h, &a, dynamic, {});
        f.pfnDynamicConstantBufferMapDiscard(h, dynamic, 0, D3D10_DDI_MAP_WRITE_DISCARD, 0, &mapped);
        TEST(last_error == S_OK && mapped.pData);
        std::memset(mapped.pData, 0x5c, bytes);
        f.pfnDynamicConstantBufferUnmap(h, dynamic, 0);
        f.pfnResourceCopy(h, staging, dynamic);
        f.pfnResourceMap(h, staging, 0, D3D10_DDI_MAP_READ, 0, &mapped);
        TEST(last_error == S_OK && mapped.pData);
        for (UINT i = 0; i < bytes; ++i) TEST(static_cast<unsigned char *>(mapped.pData)[i] == 0x5c);
        f.pfnResourceUnmap(h, staging, 0);
        f.pfnDestroyResource(h, dynamic); f.pfnDestroyResource(h, staging); f.pfnDestroyResource(h, target);
        TEST(counters.live_allocations == 1 && !memory[index].empty()); // Bound state retains ownership.
        const D3D10DDI_HRESOURCE empty{};
        f.pfnVsSetConstantBuffers(h, 0, 1, &empty);
        TEST(last_error == S_OK && !counters.live_allocations && memory[index].empty());
        // Rejected size and binding descriptors must allocate nothing.
        a.Usage = D3D10_DDI_USAGE_DEFAULT; a.MapFlags = 0;
        for (const UINT bad : {0u, 15u, 65552u}) {
            mip.TexelWidth = bad;
            f.pfnCreateResource(h, &a, target, {});
            TEST(last_error == E_INVALIDARG && !counters.live_allocations);
            f.pfnDestroyResource(h, target);
        }
        last_error = S_OK;
    }
    if (test_geometry_buffers(h, f)) return 1;
    // Shader-readable buffer storage has the same byte ownership and staging
    // contract. Typed SRV interpretation is independently validated/rejected.
    for(const auto usage:{D3D10_DDI_USAGE_DEFAULT,D3D10_DDI_USAGE_IMMUTABLE,D3D10_DDI_USAGE_DYNAMIC}) {
        constexpr UINT bytes=32769;std::vector<uint8_t> input(bytes);
        for(UINT i=0;i<bytes;++i)input[i]=uint8_t(i*37);
        D3D10DDI_MIPINFO mip{};mip.TexelWidth=bytes;mip.TexelHeight=mip.TexelDepth=1;
        D3D10_DDIARG_SUBRESOURCE_UP up{};up.pSysMem=input.data();
        D3D10DDIARG_CREATERESOURCE a{};a.ResourceDimension=D3D10DDIRESOURCE_BUFFER;
        a.MipLevels=a.ArraySize=a.SampleDesc.Count=1;a.pMipInfoList=&mip;
        a.Usage=usage;a.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;a.pInitialDataUP=&up;
        a.MapFlags=usage==D3D10_DDI_USAGE_DYNAMIC ? D3D10_DDI_CPU_ACCESS_WRITE : 0;
        alignas(Resource) unsigned char backing[2][sizeof(Resource)]{};
        D3D10DDI_HRESOURCE source{},stage{};source.pDrvPrivate=backing[0];stage.pDrvPrivate=backing[1];
        f.pfnCreateResource(h,&a,source,{});TEST(last_error==S_OK);
        a.Usage=D3D10_DDI_USAGE_STAGING;a.BindFlags=0;a.MapFlags=D3D10_DDI_CPU_ACCESS_READ;a.pInitialDataUP=nullptr;
        f.pfnCreateResource(h,&a,stage,{});f.pfnResourceCopy(h,stage,source);
        D3D10DDI_MAPPED_SUBRESOURCE mapped{};f.pfnStagingResourceMap(h,stage,0,D3D10_DDI_MAP_READ,0,&mapped);
        TEST(last_error==S_OK && mapped.pData && !std::memcmp(mapped.pData,input.data(),bytes));
        f.pfnStagingResourceUnmap(h,stage,0);
        alignas(View) unsigned char views[2][sizeof(View)]{};D3D10DDI_HSHADERRESOURCEVIEW view{},bad{};
        view.pDrvPrivate=views[0];bad.pDrvPrivate=views[1];
        D3D10DDIARG_CREATESHADERRESOURCEVIEW desc{};desc.hDrvResource=source;desc.ResourceDimension=D3D10DDIRESOURCE_BUFFER;
        desc.Format=DXGI_FORMAT_R8_UINT;desc.Buffer.FirstElement=7;desc.Buffer.NumElements=bytes-9;
        f.pfnCreateShaderResourceView(h,&desc,view,{});TEST(last_error==S_OK);
        f.pfnVsSetShaderResources(h,3,1,&view);f.pfnGsSetShaderResources(h,5,1,&view);f.pfnPsSetShaderResources(h,0,1,&view);
        TEST(last_error==S_OK && !device(h).bound_source && device(h).shader_sources[2][0].buffer &&
            device(h).shader_sources[2][0].format==62 && device(h).shader_sources[2][0].first_element==7 &&
            device(h).shader_sources[2][0].elements==bytes-9);
        const auto valid=desc;const auto handle=resource(source).allocation->handle;
        for(UINT invalid=0;invalid<4;++invalid){
            desc=valid;if(invalid==0)desc.Buffer.FirstElement=UINT_MAX;if(invalid==1)desc.Buffer.NumElements=0;
            if(invalid==2)desc.Buffer.NumElements=bytes;if(invalid==3)desc.Format=DXGI_FORMAT_R8_UNORM;
            f.pfnCreateShaderResourceView(h,&desc,bad,{});TEST(FAILED(last_error));last_error=S_OK;
            f.pfnPsSetShaderResources(h,0,1,&bad);TEST(FAILED(last_error) && device(h).shader_sources[2][0].allocation->handle==handle);
            f.pfnDestroyShaderResourceView(h,bad);last_error=S_OK;
        }
        f.pfnDestroyShaderResourceView(h,view);f.pfnDestroyResource(h,source);f.pfnDestroyResource(h,stage);
        TEST(counters.live_allocations==1 && !memory[handle].empty());
        const D3D10DDI_HSHADERRESOURCEVIEW empty{};
        f.pfnVsSetShaderResources(h,3,1,&empty);f.pfnGsSetShaderResources(h,5,1,&empty);f.pfnPsSetShaderResources(h,0,1,&empty);
        TEST(last_error==S_OK && !counters.live_allocations && memory[handle].empty());
    }
    f.pfnDestroyDevice(h);
    TEST(!counters.live_contexts && !counters.live_allocations && !counters.cleanup_errors && !contract_error);
    return 0;
}
