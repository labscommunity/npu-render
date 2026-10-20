// Resource movement uses explicit CPU staging and the ordered kernel copy path.
// This is not a shader draw and is never counted as NPU execution.
void APIENTRY copy_region(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE destination, UINT dst_sub,
                         UINT x, UINT y, UINT z, D3D10DDI_HRESOURCE source, UINT src_sub,
                         const D3D10_DDI_BOX *box) {
    guard(h, __FUNCTION__, [&] {
        auto &d=resource(destination); auto &s=resource(source);
        const auto da=mip_allocation(d,dst_sub),sa=mip_allocation(s,src_sub);
        require(da!=sa && da->handle!=sa->handle &&
                da->context==device(h).context && sa->context==da->context && !da->context->poisoned &&
                !da->locked && !sa->locked && d.usage!=D3D10_DDI_USAGE_IMMUTABLE &&
                bool(d.buffer_bytes)==bool(s.buffer_bytes) && da->surface.format==sa->surface.format);
        const UINT sw=s.buffer_bytes?s.buffer_bytes:sa->surface.width;
        const UINT sh=s.buffer_bytes?1:sa->surface.height;
        const UINT dw=d.buffer_bytes?d.buffer_bytes:da->surface.width;
        const UINT dh=d.buffer_bytes?1:da->surface.height;
        const auto region=box?*box:D3D10_DDI_BOX{0,0,0,LONG(sw),LONG(sh),1};
        // The DDI defines reversed extents as empty, not an invalid copy.
        if(region.left>=region.right || region.top>=region.bottom || region.front>=region.back)return;
        require(region.left>=0 && region.top>=0 && region.front==0 && region.back==1 && !z &&
                UINT(region.right)<=sw && UINT(region.bottom)<=sh &&
                uint64_t(x)+UINT(region.right-region.left)<=dw &&
                uint64_t(y)+UINT(region.bottom-region.top)<=dh);
        resource_boundary("copy_region_source",sa.get(),0,src_sub);
        uint64_t moved=0;
        if(s.buffer_bytes){
            require(!y && !sa->runtime_resource && !da->runtime_resource);
            const auto bytes=UINT(region.right-region.left);std::vector<uint8_t> snapshot(bytes);
            {Mapping read(*sa,true);std::memcpy(snapshot.data(),static_cast<const uint8_t *>(read.data)+region.left,bytes);read.finish();}
            {Mapping write(*da,false);std::memcpy(static_cast<uint8_t *>(write.data)+x,snapshot.data(),bytes);write.finish();}
            moved=bytes;counters.upload_bytes+=bytes;resource_written("buffer_region_written",*da);
        }else{
            const auto pixels=read_pixels(*sa);
            const UINT width=UINT(region.right-region.left),height=UINT(region.bottom-region.top);
            const UINT bytes=texel_bytes(sa->surface.format);
            const D3D10_DDI_BOX target{LONG(x),LONG(y),0,LONG(x+width),LONG(y+height),1};
            upload(*da,&target,pixels.data()+(size_t(region.top)*sw+region.left)*bytes,sw*bytes);
            moved=uint64_t(width)*height*bytes;
        }
        counters.copies+=moved;
        lab_trace("{\"event\":\"resource_region_copied\",\"bytes\":%llu,\"cpu_staging\":true,\"npu_execution\":false}\n",
                  static_cast<unsigned long long>(moved));
    });
}
