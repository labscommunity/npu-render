// Actual UMD entrypoints with an explicitly emulated, lock-retired copy queue.
// This checks ownership/error paths; the guest probe checks real VidMm execution.
int test_uploads(const D3DDDI_DEVICECALLBACKS &kt, const D3D10DDI_CORELAYER_DEVICECALLBACKS &um) {
    render_failure = false; emulate_copies = true; last_error = S_OK;
    alignas(Device) unsigned char storage[sizeof(Device)]{};
    D3D10DDI_DEVICEFUNCS f{}; DXGI_DDI_BASE_FUNCTIONS dxgi{};
    D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate = storage; create.hRTDevice.handle = runtime_handle;
    create.pDeviceFuncs = &f; create.pUMCallbacks = &um; create.pKTCallbacks = &kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions = &dxgi; create.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({}, &create) == S_OK);
    const auto h = create.hDrvDevice;
    for (const auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM}) {
        for (bool shared : {false, true}) {
            constexpr UINT width = 65, height = 7, pitch = width * 4 + 19;
            std::vector<unsigned char> input(pitch * height), expected(4096);
            for (UINT i = 0; i < input.size(); ++i) input[i] = static_cast<unsigned char>(i * 73 + 11);
            for (UINT y = 0; y < height; ++y) std::memcpy(expected.data() + y * 512, input.data() + y * pitch, width * 4);
            const auto uploads_before = counters.upload_bytes;
            D3D10DDI_MIPINFO mip{}; mip.TexelWidth = width; mip.TexelHeight = height; mip.TexelDepth = 1;
            D3D10_DDIARG_SUBRESOURCE_UP initial{}; initial.pSysMem = input.data(); initial.SysMemPitch = pitch;
            D3D10DDIARG_CREATERESOURCE args{};
            args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D; args.MipLevels = args.ArraySize = 1;
            args.pMipInfoList = &mip; args.Format = format; args.SampleDesc.Count = 1;
            args.Usage = D3D10_DDI_USAGE_DEFAULT; args.BindFlags = D3D10_DDI_BIND_RENDER_TARGET;
            args.MiscFlags = shared ? D3D10_DDI_RESOURCE_MISC_SHARED : 0; args.pInitialDataUP = &initial;
            alignas(Resource) unsigned char target_store[sizeof(Resource)]{};
            D3D10DDI_HRESOURCE target{}; target.pDrvPrivate = target_store;
            D3D10DDI_HRTRESOURCE rt{}; rt.handle = shared ? reinterpret_cast<HANDLE>(uintptr_t(0x9003)) : nullptr;
            f.pfnCreateResource(h, &args, target, rt);
            TEST(last_error == S_OK && counters.live_allocations == 1 && pending_copies.empty());
            const auto index = resource(target).allocation->handle;
            TEST(memory[index] == expected && counters.upload_bytes == uploads_before + width * height * 4);
            // The caller can immediately reuse its memory after the API returns.
            std::fill(input.begin(), input.end(), static_cast<unsigned char>(0xc7));
            TEST(memory[index] == expected);
            const D3D10_DDI_BOX region{2, 3, 0, 7, 6, 1};
            f.pfnResourceUpdateSubresourceUP(h, target, 0, &region, input.data(), pitch, 0);
            for (LONG y = region.top; y < region.bottom; ++y)
                std::memset(expected.data() + y * 512 + region.left * 4, 0xc7, 20);
            TEST(last_error == S_OK && memory[index] == expected && pending_copies.empty());
            TEST(counters.upload_bytes == uploads_before + width * height * 4 + 60);
            // Empty updates read no source data and perform no allocation/copy.
            const auto submits = counters.submitted_copies, allocated = counters.allocations;
            const D3D10_DDI_BOX empty{2, 3, 0, 2, 6, 1};
            f.pfnResourceUpdateSubresourceUP(h, target, 0, &empty, nullptr, 0, 0);
            TEST(last_error == S_OK && counters.submitted_copies == submits && counters.allocations == allocated);
            const D3D10_DDI_BOX invalid[] = {{0, 0, 0, 66, 7, 1}, {7, 0, 0, 2, 7, 1},
                                           {0, 0, 0, 65, 8, 1}, {0, 0, 0, 65, 7, 2},
                                           {-1, 0, 0, 65, 7, 1}, {0, -1, 0, 65, 7, 1}, {0, 0, -1, 65, 7, 1}};
            for (const auto &box : invalid) {
                f.pfnResourceUpdateSubresourceUP(h, target, 0, &box, input.data(), pitch, 0);
                TEST(last_error == E_INVALIDARG && memory[index] == expected);
            }
            f.pfnResourceUpdateSubresourceUP(h, target, 0, nullptr, input.data(), width * 4 - 1, 0);
            TEST(last_error == E_INVALIDARG && memory[index] == expected);
            f.pfnResourceUpdateSubresourceUP(h, target, 0, nullptr, nullptr, pitch, 0);
            TEST(last_error == E_INVALIDARG && memory[index] == expected);
            f.pfnResourceUpdateSubresourceUP(h, target, 1, nullptr, input.data(), pitch, 0);
            TEST(last_error == E_INVALIDARG && memory[index] == expected);
            f.pfnResourceUpdateSubresourceUP(h, target, 0, nullptr, reinterpret_cast<const void *>(UINTPTR_MAX - 3), pitch, 0);
            TEST(last_error == E_INVALIDARG && memory[index] == expected);
            auto &allocation = *resource(target).allocation;
            const auto own_context = allocation.context;
            allocation.context = std::make_shared<Context>(counters);
            f.pfnResourceUpdateSubresourceUP(h, target, 0, nullptr, input.data(), pitch, 0);
            TEST(last_error == E_INVALIDARG && memory[index] == expected);
            allocation.context = own_context;
            if (!shared) {
                allocation.lock(true);
                f.pfnResourceUpdateSubresourceUP(h, target, 0, nullptr, input.data(), pitch, 0);
                TEST(last_error == E_INVALIDARG && memory[index] == expected && allocation.locked);
                TEST(allocation.unlock() == S_OK);
            }
            TEST(counters.submitted_copies == submits && counters.allocations == allocated);
            lock_failure = true;
            f.pfnResourceUpdateSubresourceUP(h, target, 0, nullptr, input.data(), pitch, 0);
            TEST(last_error == E_ACCESSDENIED && memory[index] == expected && counters.live_allocations == 1);
            lock_failure = false; last_error = S_OK;
            f.pfnResourceUpdateSubresourceUP(h, target, 0, nullptr, input.data(), pitch, 0);
            for (UINT y = 0; y < height; ++y) std::memset(expected.data() + y * 512, 0xc7, width * 4);
            TEST(last_error == S_OK && memory[index] == expected && pending_copies.empty());
            f.pfnDestroyResource(h, target);
            TEST(!counters.live_allocations && resource_handles.empty());
        }
    }
    f.pfnDestroyDevice(h);
    TEST(!counters.live_contexts && !counters.live_allocations && !counters.cleanup_errors && !contract_error);
    emulate_copies = false;
    return 0;
}
