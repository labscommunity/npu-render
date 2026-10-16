// Exercise the actual DXGI rotation entry with owned runtime shims. Runtime
// backing association changes are emulated explicitly, separately from the UMD.
int test_rotation(const D3DDDI_DEVICECALLBACKS &kt, const D3D10DDI_CORELAYER_DEVICECALLBACKS &um) {
    for (bool shared : {false,true}) {
        alignas(Device) unsigned char storage[sizeof(Device)]{};
        D3D10DDI_DEVICEFUNCS f{}; DXGI_DDI_BASE_FUNCTIONS dxgi{}; DXGI_DDI_BASE_CALLBACKS cb{};
        cb.pfnPresentCb = present_cb; present_callback_status = S_OK;
        D3D10DDIARG_CREATEDEVICE create{};
        create.hDrvDevice.pDrvPrivate = storage; create.hRTDevice.handle = runtime_handle;
        create.pDeviceFuncs = &f; create.pUMCallbacks = &um; create.pKTCallbacks = &kt;
        create.DXGIBaseDDI.pDXGIDDIBaseFunctions = &dxgi; create.DXGIBaseDDI.pDXGIBaseCallbacks = &cb;
        create.Interface = D3D10_0_DDI_INTERFACE_VERSION;
        TEST(create_device({}, &create) == DXGI_STATUS_NO_REDIRECTION);
        const auto h = create.hDrvDevice; auto &d = device(h);
        std::array<Resource,3> resources{}; std::array<D3DKMT_HANDLE,3> handles{};
        std::array<HANDLE,3> runtime{}; std::array<DXGI_DDI_HRESOURCE,3> list{};
        for (UINT i=0;i<3;++i) {
            auto &r=resources[i]; r.usage=D3D10_DDI_USAGE_DEFAULT; r.bind_flags=D3D10_DDI_BIND_PRESENT | D3D10_DDI_BIND_RENDER_TARGET;
            r.allocation=std::make_shared<Allocation>(d.context);
            runtime[i]=shared?reinterpret_cast<HANDLE>(uintptr_t(0x9900+i)):nullptr;
            r.allocation->create(65,13,87,runtime[i]); handles[i]=r.allocation->handle; list[i]=UINT_PTR(&r);
            std::fill(memory[handles[i]].begin(),memory[handles[i]].end(),static_cast<unsigned char>(23+i));
        }
        alignas(View) unsigned char view_storage[sizeof(View)]{};
        D3D10DDI_HRENDERTARGETVIEW view{}; view.pDrvPrivate=view_storage;
        D3D10DDIARG_CREATERENDERTARGETVIEW desc{}; desc.hDrvResource.pDrvPrivate=&resources[0];
        desc.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.Tex2D.ArraySize=1;
        last_error=S_OK; f.pfnCreateRenderTargetView(h,&desc,view,{}); TEST(last_error==S_OK);
        f.pfnSetRenderTargets(h,&view,1,0,{}); TEST(last_error==S_OK);
        const auto pixels=memory;
        DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES args{}; args.hDevice=UINT_PTR(h.pDrvPrivate); args.pResources=list.data(); args.Resources=3;
        const auto valid=args;
        TEST(dxgi.pfnRotateResourceIdentities(nullptr)==E_INVALIDARG);
        for (UINT bad=0;bad<12;++bad) {
            switch (bad) {
            case 0: args.Resources=0; break;
            case 1: args.Resources=17; break;
            case 2: args.pResources=nullptr; break;
            case 3: list[2]=0; break;
            case 4: list[2]=list[0]; break;
            case 5: resources[2].bind_flags=0; break;
            case 6: resources[2].allocation->surface.width=64; break;
            case 7: resources[2].allocation->locked=true; break;
            case 8: resources[2].mapped_write=true; break;
            case 9: d.context->poisoned=true; break;
            case 10: resources[2].allocation->handle=handles[0]; break;
            case 11: resources[2].allocation->context.reset(); break;
            }
            TEST(dxgi.pfnRotateResourceIdentities(&args)==E_INVALIDARG && memory==pixels);
            for(UINT i=0;i<2;++i) TEST(resources[i].allocation->handle==handles[i] && resources[i].allocation->runtime_resource==runtime[i]);
            args=valid; list[2]=UINT_PTR(&resources[2]); resources[2].bind_flags=resources[0].bind_flags;
            resources[2].allocation->surface=resources[0].allocation->surface; resources[2].allocation->locked=false;
            resources[2].mapped_write=false; d.context->poisoned=false; resources[2].allocation->handle=handles[2]; resources[2].allocation->context=d.context;
        }
        args.Resources=1; TEST(dxgi.pfnRotateResourceIdentities(&args)==S_OK && resources[0].allocation->handle==handles[0]); args=valid;
        for (UINT turn=0;turn<3;++turn) {
            TEST(dxgi.pfnRotateResourceIdentities(&args)==S_OK && memory==pixels);
            for(UINT i=0;i<3;++i) {
                const auto expected=handles[(i+turn+1)%3];
                TEST(resources[i].allocation->handle==expected && resources[i].allocation->runtime_resource==runtime[i]);
                if(shared) resource_handles[runtime[i]]=expected; // runtime side, never done by UMD
            }
            TEST(static_cast<View *>(view.pDrvPrivate)->allocation->handle==handles[(turn+1)%3]);
            TEST(d.bound_target->handle==handles[(turn+1)%3]);
            DXGI_DDI_ARG_PRESENT present_args{}; present_args.hDevice=args.hDevice; present_args.hSurfaceToPresent=list[0]; present_args.Flags.Value=shared?2u:1u;
            TEST(dxgi.pfnPresent(&present_args)==S_OK && observed_present.hSrcAllocation==handles[(turn+1)%3]);
        }
        f.pfnSetRenderTargets(h,nullptr,0,0,{}); f.pfnDestroyRenderTargetView(h,view);
        // Release in a different order to detect leaks or duplicate ownership.
        for(UINT i : {1u,0u,2u}) resources[i].allocation.reset();
        f.pfnDestroyDevice(h); TEST(!counters.live_contexts && !counters.live_allocations && !counters.cleanup_errors && !contract_error);
    }
    std::puts("{\"kind\":\"registered-rotation-shim\",\"shared_and_private\":true,\"view_and_binding_follow_backing\":true,\"display_delivery_tested\":false}");
    return 0;
}
