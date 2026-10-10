// Exercise CPU access through the actual DDI, including copies crossing a
// runtime-owned allocation that cannot itself be CPU-locked by this driver.
int test_staging_maps(const D3DDDI_DEVICECALLBACKS &kt, const D3D10DDI_CORELAYER_DEVICECALLBACKS &um) {
    render_failure = lock_failure = false; emulate_copies = true; last_error = S_OK;
    alignas(Device) unsigned char device_storage[sizeof(Device)]{};
    D3D10DDI_DEVICEFUNCS f{}; DXGI_DDI_BASE_FUNCTIONS dxgi{};
    D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate = device_storage; create.hRTDevice.handle = runtime_handle;
    create.pDeviceFuncs = &f; create.pUMCallbacks = &um; create.pKTCallbacks = &kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions = &dxgi; create.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({}, &create) == DXGI_STATUS_NO_REDIRECTION);
    const auto h = create.hDrvDevice;
    constexpr UINT read = D3D10_DDI_CPU_ACCESS_READ, write = D3D10_DDI_CPU_ACCESS_WRITE;
    for (bool shared : {false, true}) for (UINT access : {read, write, read | write}) {
        constexpr UINT width = 26, height = 26;
        std::vector<uint32_t> expected(width * height);
        for (UINT i = 0; i < expected.size(); ++i) expected[i] = 0x80000000u + i;
        D3D10DDI_MIPINFO mip{}; mip.TexelWidth = width; mip.TexelHeight = height; mip.TexelDepth = 1;
        D3D10_DDIARG_SUBRESOURCE_UP initial{}; initial.pSysMem = expected.data(); initial.SysMemPitch = width * 4;
        D3D10DDIARG_CREATERESOURCE args{};
        args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D; args.MipLevels = args.ArraySize = args.SampleDesc.Count = 1;
        args.pMipInfoList = &mip; args.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        args.Usage = D3D10_DDI_USAGE_DEFAULT; args.BindFlags = D3D10_DDI_BIND_RENDER_TARGET;
        args.MiscFlags = shared ? D3D10_DDI_RESOURCE_MISC_SHARED : 0; args.pInitialDataUP = &initial;
        alignas(Resource) unsigned char stores[3][sizeof(Resource)]{};
        D3D10DDI_HRESOURCE source{}, staging{}, readback{};
        source.pDrvPrivate = stores[0]; staging.pDrvPrivate = stores[1]; readback.pDrvPrivate = stores[2];
        D3D10DDI_HRTRESOURCE rt{}; rt.handle = shared ? reinterpret_cast<HANDLE>(uintptr_t(0x9034)) : nullptr;
        f.pfnCreateResource(h, &args, source, rt); TEST(last_error == S_OK);
        args.Usage = D3D10_DDI_USAGE_STAGING; args.BindFlags = args.MiscFlags = 0; args.pInitialDataUP = nullptr;
        args.MapFlags = access; f.pfnCreateResource(h, &args, staging, {}); TEST(last_error == S_OK);
        args.MapFlags = read; f.pfnCreateResource(h, &args, readback, {}); TEST(last_error == S_OK);
        f.pfnResourceCopy(h, staging, source); TEST(last_error == S_OK);
        // A nonblocking map reports transient contention with the D3D10 DDI
        // status and preserves ownership. A later retry succeeds normally.
        D3D10DDI_MAPPED_SUBRESOURCE probe{};const auto probe_mode=(access&read)?D3D10_DDI_MAP_READ:D3D10_DDI_MAP_WRITE;
        const auto pending=pending_copies;lock_busy=true;
        f.pfnStagingResourceMap(h,staging,0,probe_mode,D3D10_DDI_MAP_FLAG_DONOTWAIT,&probe);
        TEST(last_error==DXGI_DDI_ERR_WASSTILLDRAWING && !probe.pData && !resource(staging).allocation->locked && pending_copies==pending);
        lock_busy=false;last_error=S_OK;
        f.pfnStagingResourceMap(h,staging,0,probe_mode,D3D10_DDI_MAP_FLAG_DONOTWAIT,&probe);
        TEST(last_error==S_OK && probe.pData && last_lock_do_not_wait);
        f.pfnStagingResourceUnmap(h,staging,0);TEST(last_error==S_OK);
        f.pfnStagingResourceMap(h,staging,0,probe_mode,D3D10_DDI_MAP_FLAG_DONOTWAIT|1,&probe);
        TEST(last_error==E_INVALIDARG && !probe.pData && !resource(staging).allocation->locked);last_error=S_OK;
        const auto before = counters.upload_bytes; UINT writes = 0;
        for (const auto mode : {D3D10_DDI_MAP_READ, D3D10_DDI_MAP_WRITE, D3D10_DDI_MAP_READWRITE}) {
            const bool reads = mode != D3D10_DDI_MAP_WRITE, modifies = mode != D3D10_DDI_MAP_READ;
            const bool allowed = (!reads || (access & read)) && (!modifies || (access & write));
            D3D10DDI_MAPPED_SUBRESOURCE mapped{};
            f.pfnStagingResourceMap(h, staging, 0, mode, 0, &mapped);
            if (!allowed) {
                TEST(last_error == E_INVALIDARG && !mapped.pData && !resource(staging).allocation->locked);
                last_error = S_OK; continue;
            }
            TEST(last_error == S_OK && mapped.pData && last_lock_read_only == (reads && !modifies) &&
                last_lock_write_only == (modifies && !reads) && !last_lock_do_not_wait);
            for (UINT y = 0; y < height; ++y) {
                auto row = static_cast<unsigned char *>(mapped.pData) + y * mapped.RowPitch;
                if (reads) TEST(!std::memcmp(row, expected.data() + y * width, width * 4));
                if (modifies) {
                    for (UINT x = 0; x < width; ++x) expected[y * width + x] ^= 0x00224466u;
                    std::memcpy(row, expected.data() + y * width, width * 4);
                }
            }
            f.pfnStagingResourceUnmap(h, staging, 0); TEST(last_error == S_OK);
            if (modifies) ++writes;
        }
        TEST(counters.upload_bytes == before + uint64_t(width) * height * 4 * writes);
        f.pfnResourceCopy(h, source, staging); TEST(last_error == S_OK);
        f.pfnResourceCopy(h, readback, source); TEST(last_error == S_OK);
        D3D10DDI_MAPPED_SUBRESOURCE mapped{};
        f.pfnStagingResourceMap(h, readback, 0, D3D10_DDI_MAP_READ, 0, &mapped); TEST(last_error == S_OK);
        for (UINT y = 0; y < height; ++y)
            TEST(!std::memcmp(static_cast<unsigned char *>(mapped.pData) + y * mapped.RowPitch, expected.data() + y * width, width * 4));
        f.pfnStagingResourceUnmap(h, readback, 0); TEST(last_error == S_OK && pending_copies.empty());
        for (auto resource : {readback, staging, source}) f.pfnDestroyResource(h, resource);
        TEST(last_error == S_OK && !counters.live_allocations && resource_handles.empty());
    }
    f.pfnDestroyDevice(h); emulate_copies = false;
    TEST(!counters.live_contexts && !counters.cleanup_errors && !contract_error);
    std::puts("{\"staging_texture_maps\":true,\"access_modes\":3,\"shared_and_private_roundtrip\":true,\"read_write_lock_flags\":true,\"nonblocking_contention_and_retry\":true,\"denied_access_rejected\":true,\"npu_execution_tested\":false}");
    return 0;
}
