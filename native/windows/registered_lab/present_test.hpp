namespace {
DXGIDDICB_PRESENT observed_present{};
UINT present_callback_calls = 0;
UINT replaced_present_callback_calls = 0;
HRESULT present_callback_status = S_OK;
HRESULT APIENTRY present_cb(HANDLE runtime, DXGIDDICB_PRESENT *args) {
    verify(runtime == runtime_handle && args);
    if (!args) return E_INVALIDARG;
    observed_present = *args; ++present_callback_calls;
    return present_callback_status;
}
HRESULT APIENTRY replaced_present_cb(HANDLE runtime, DXGIDDICB_PRESENT *args) {
    ++replaced_present_callback_calls;
    return present_cb(runtime, args);
}
}
int test_present(const D3DDDI_DEVICECALLBACKS &kt, const D3D10DDI_CORELAYER_DEVICECALLBACKS &um) {
    alignas(Device) unsigned char storage[sizeof(Device)]{};
    D3D10DDI_DEVICEFUNCS f{}; DXGI_DDI_BASE_FUNCTIONS dxgi{}; DXGI_DDI_BASE_CALLBACKS cb{};
    cb.pfnPresentCb = present_cb;
    D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate = storage; create.hRTDevice.handle = runtime_handle;
    create.pDeviceFuncs = &f; create.pUMCallbacks = &um; create.pKTCallbacks = &kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions = &dxgi; create.DXGIBaseDDI.pDXGIBaseCallbacks = &cb;
    create.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({}, &create) == S_OK);
    const auto h = create.hDrvDevice;
    // The live DXGI discard swap chain requests PRESENT|RENDER_TARGET with
    // DISCARD_ON_PRESENT and no primary description. It remains a real VidMm
    // allocation; discarding permits undefined old contents, not fake scanout.
    D3D10DDI_MIPINFO mip{}; mip.TexelWidth = 65; mip.TexelHeight = 13; mip.TexelDepth = 1;
    mip.PhysicalWidth = 65; mip.PhysicalHeight = 13; mip.PhysicalDepth = 1;
    D3D10DDIARG_CREATERESOURCE descriptor{};
    descriptor.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D; descriptor.MipLevels = descriptor.ArraySize = 1;
    descriptor.pMipInfoList = &mip; descriptor.SampleDesc.Count = 1; descriptor.Usage = D3D10_DDI_USAGE_DEFAULT;
    descriptor.BindFlags = D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_PRESENT;
    alignas(Resource) unsigned char backbuffer[sizeof(Resource)]{};
    D3D10DDI_HRESOURCE back{}; back.pDrvPrivate = backbuffer;
    for (auto format : {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM}) for (UINT discard = 0; discard < 2; ++discard) {
        descriptor.Format = format; descriptor.MiscFlags = discard ? D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT : 0;
        last_error = S_OK; f.pfnCreateResource(h, &descriptor, back, {});
        TEST(last_error == S_OK && resource(back).allocation->handle && counters.live_allocations == 1);
        TEST(resource(back).allocation->surface.width == 65 && resource(back).allocation->surface.format == UINT(format));
        f.pfnDestroyResource(h, back); TEST(!counters.live_allocations);
    }
    const auto valid_descriptor = descriptor;
    DXGI_DDI_PRIMARY_DESC primary{};
    for (UINT failure = 0; failure < 5; ++failure) {
        switch (failure) {
        case 0: descriptor.BindFlags = D3D10_DDI_BIND_RENDER_TARGET; break;
        case 1: descriptor.pPrimaryDesc = &primary; break;
        case 2: descriptor.MiscFlags = 4; break;
        case 3: descriptor.Usage = D3D10_DDI_USAGE_STAGING; break;
        case 4: descriptor.MapFlags = D3D10_DDI_CPU_ACCESS_READ; break;
        }
        last_error = S_OK; f.pfnCreateResource(h, &descriptor, back, {});
        TEST(last_error == E_INVALIDARG && !counters.live_allocations);
        f.pfnDestroyResource(h, back); descriptor = valid_descriptor;
    }
    last_error = S_OK;
    Resource source{}, destination{}; source.usage = destination.usage = D3D10_DDI_USAGE_DEFAULT;
    source.allocation = std::make_shared<Allocation>(device(h).context);
    destination.allocation = std::make_shared<Allocation>(device(h).context);
    source.allocation->create(65, 13, 87); destination.allocation->create(65, 13, 87);
    const auto pixels_before = memory;
    DXGI_DDI_ARG_PRESENT args{};
    args.hDevice = UINT_PTR(h.pDrvPrivate); args.hSurfaceToPresent = UINT_PTR(&source); args.hDstResource = UINT_PTR(&destination);
    args.Flags.Blt = 1; args.pDXGIContext = reinterpret_cast<void *>(uintptr_t(0x4321));
    const auto original = args;
    for (UINT failure = 0; failure < 9; ++failure) {
        switch (failure) {
        case 0: args.Flags.Flip = 1; break;
        case 1: args.SrcSubResourceIndex = 1; break;
        case 2: args.DstSubResourceIndex = 1; break;
        case 3: args.hSurfaceToPresent = 0; break;
        case 4: args.hDstResource = args.hSurfaceToPresent; break;
        case 5: source.allocation->locked = true; break;
        case 6: destination.allocation->surface.format = 28; break;
        case 7: device(h).dxgi_callbacks = nullptr; break;
        case 8: device(h).context->poisoned = true; break;
        }
        TEST(dxgi.pfnPresent(&args) == E_INVALIDARG && !present_callback_calls && memory == pixels_before);
        args = original; source.allocation->locked = false; destination.allocation->surface.format = 87;
        device(h).dxgi_callbacks = &cb; device(h).context->poisoned = false;
    }
    TEST(dxgi.pfnPresent(&args) == S_OK && present_callback_calls == 1 && memory == pixels_before);
    TEST(observed_present.hSrcAllocation == source.allocation->handle && observed_present.hDstAllocation == destination.allocation->handle &&
         observed_present.hContext == context_handle && observed_present.pDXGIContext == args.pDXGIContext &&
         !observed_present.BroadcastContextCount && !observed_present.PrivateDriverDataSize && !observed_present.SyncIntervalOverrideValid);
    args.hDstResource = 0; present_callback_status = DXGI_STATUS_OCCLUDED;
    cb.pfnPresentCb = replaced_present_cb;
    TEST(dxgi.pfnPresent(&args) == DXGI_STATUS_OCCLUDED && present_callback_calls == 2 && !observed_present.hDstAllocation);
    TEST(replaced_present_callback_calls == 1);
    present_callback_status = E_ACCESSDENIED;
    TEST(dxgi.pfnPresent(&args) == E_ACCESSDENIED && present_callback_calls == 3 && device(h).context->poisoned);
    TEST(dxgi.pfnPresent(&args) == E_INVALIDARG && present_callback_calls == 3 && memory == pixels_before);
    NrFillStats stats{}; TEST(nr_fill_stats(&stats));
    TEST(stats.present_requests == 3 && stats.accepted_present_callbacks == 2);
    stats.version = 4; TEST(!nr_fill_stats(&stats));
    source.allocation.reset(); destination.allocation.reset(); f.pfnDestroyDevice(h);
    TEST(!counters.live_contexts && !counters.live_allocations && !counters.cleanup_errors && !contract_error);
    std::puts("{\"kind\":\"registered-present-callback-shim\",\"callback_requests\":3,\"accepted_callbacks\":2,\"display_delivery_tested\":false}");
    return 0;
}
