int test_copy_regions(const D3DDDI_DEVICECALLBACKS &kt, const D3D10DDI_CORELAYER_DEVICECALLBACKS &um) {
    emulate_copies=true;render_failure=false;last_error=S_OK;
    alignas(Device) unsigned char storage[sizeof(Device)]{};
    D3D10DDI_DEVICEFUNCS f{};DXGI_DDI_BASE_FUNCTIONS dxgi{};D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate=storage;create.hRTDevice.handle=runtime_handle;
    create.pDeviceFuncs=&f;create.pUMCallbacks=&um;create.pKTCallbacks=&kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions=&dxgi;create.Interface=D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({},&create)==DXGI_STATUS_NO_REDIRECTION);
    const auto h=create.hDrvDevice;auto &d=device(h);const auto executions=counters.executions;
    for(bool buffer:{false,true})for(bool shared_source:{false,true})for(bool shared_dest:{false,true})for(UINT format:{28u,87u}){
        if(buffer && (shared_source || shared_dest || format==87))continue;
        Resource source{},destination{};source.usage=destination.usage=D3D10_DDI_USAGE_DEFAULT;
        source.allocation=std::make_shared<Allocation>(d.context);destination.allocation=std::make_shared<Allocation>(d.context);
        source.buffer_bytes=buffer?513:0;destination.buffer_bytes=buffer?777:0;
        source.allocation->create(buffer?129:56,buffer?1:5,format,shared_source?reinterpret_cast<HANDLE>(uintptr_t(0xab01)):nullptr);
        destination.allocation->create(buffer?195:65,buffer?1:7,format,shared_dest?reinterpret_cast<HANDLE>(uintptr_t(0xab02)):nullptr);
        const auto si=source.allocation->handle,di=destination.allocation->handle;
        for(size_t n=0;n<memory[si].size();++n)memory[si][n]=uint8_t(n*37+9);
        std::fill(memory[di].begin(),memory[di].end(),uint8_t(0xa7));
        auto expected=memory[di];const auto source_before=memory[si];
        D3D10DDI_HRESOURCE src{},dst{};src.pDrvPrivate=&source;dst.pDrvPrivate=&destination;
        D3D10_DDI_BOX box=buffer?D3D10_DDI_BOX{21,0,0,93,1,1}:D3D10_DDI_BOX{3,1,0,17,4,1};
        const UINT x=5,y=buffer?0:2;
        const auto copy=[&]{f.pfnResourceCopyRegion(h,dst,0,x,y,0,src,0,&box);};
        last_error=S_OK;copy();TEST(last_error==S_OK && pending_copies.empty());
        if(buffer)std::memcpy(expected.data()+x,source_before.data()+box.left,size_t(box.right-box.left));
        else for(LONG row=box.top;row<box.bottom;++row)
            std::memcpy(expected.data()+(y+UINT(row-box.top))*destination.allocation->surface.row_pitch+x*4,
                        source_before.data()+size_t(row)*source.allocation->surface.row_pitch+box.left*4,size_t(box.right-box.left)*4);
        TEST(memory[di]==expected && memory[si]==source_before);
        const auto valid_box=box;const auto allocated=counters.allocations,submitted=counters.submitted_copies;
        for(UINT bad=0;bad<11;++bad){
            UINT dx=x,dy=y,dz=0,ds=0,ss=0;auto sh=src;
            switch(bad){
            case 0:ds=1;break;case 1:ss=1;break;case 2:dx=UINT_MAX;break;
            case 3:dy=UINT_MAX;break;case 4:dz=1;break;case 5:sh=dst;break;
            case 6:destination.usage=D3D10_DDI_USAGE_IMMUTABLE;break;
            case 7:source.allocation->locked=true;break;
            case 8:box.left=-1;break;case 9:box.right=20000;break;
            case 10:destination.allocation->surface.format=format==28?87:28;break;
            }
            last_error=S_OK;f.pfnResourceCopyRegion(h,dst,ds,dx,dy,dz,sh,ss,&box);
            TEST(last_error==E_INVALIDARG && memory[di]==expected && memory[si]==source_before);
            box=valid_box;source.allocation->locked=false;destination.usage=D3D10_DDI_USAGE_DEFAULT;destination.allocation->surface.format=format;
        }
        // All three reversed/empty dimensions must be no-ops, even with offsets
        // that would be invalid for a nonempty copy. No allocation or submission.
        for(UINT axis=0;axis<3;++axis){box=valid_box;if(axis==0)box.right=box.left-1;if(axis==1)box.bottom=box.top;if(axis==2)box.back=box.front;
            last_error=S_OK;f.pfnResourceCopyRegion(h,dst,0,UINT_MAX,UINT_MAX,UINT_MAX,src,0,&box);
            TEST(last_error==S_OK && memory[di]==expected && memory[si]==source_before);}
        TEST(counters.allocations==allocated && counters.submitted_copies==submitted);
        last_error=S_OK;f.pfnResourceCopyRegion(h,dst,0,0,0,0,src,0,nullptr);TEST(last_error==S_OK);
        if(buffer)std::memcpy(expected.data(),source_before.data(),source.buffer_bytes);
        else for(UINT row=0;row<source.allocation->surface.height;++row)
            std::memcpy(expected.data()+size_t(row)*destination.allocation->surface.row_pitch,
                        source_before.data()+size_t(row)*source.allocation->surface.row_pitch,source.allocation->surface.width*4);
        TEST(memory[di]==expected && memory[si]==source_before && pending_copies.empty());
        source.allocation.reset();destination.allocation.reset();TEST(!counters.live_allocations);
    }
    TEST(counters.executions==executions);f.pfnDestroyDevice(h);emulate_copies=false;
    TEST(!counters.live_contexts && !counters.live_allocations && !counters.cleanup_errors && !contract_error);
    std::puts("{\"kind\":\"registered-region-copy-shim\",\"shared_and_private\":true,\"padding_preserved\":true,\"npu_execution_tested\":false}");
    return 0;
}
