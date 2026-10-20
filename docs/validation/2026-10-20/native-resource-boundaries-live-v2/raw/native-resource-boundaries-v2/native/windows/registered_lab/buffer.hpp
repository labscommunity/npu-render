// Included after Resource/Device definitions. CPU uploads into real VidMm backing.
void update_buffer(Resource &r, const void *source) {
    require(r.buffer_bytes && source && uintptr_t(source) <= UINTPTR_MAX - r.buffer_bytes &&
            !r.allocation->locked && !r.allocation->context->poisoned);
    Mapping write(*r.allocation, false);
    std::memcpy(write.data, source, r.buffer_bytes);
    write.finish(); counters.upload_bytes += r.buffer_bytes;resource_written("buffer_upload_complete",*r.allocation);
}
void create_buffer(Device &d, const D3D10DDIARG_CREATERESOURCE &a, Resource &r) {
    require(a.MipLevels == 1 && a.ArraySize == 1 && a.pMipInfoList && a.Format == DXGI_FORMAT_UNKNOWN &&
            a.SampleDesc.Count == 1 && !a.SampleDesc.Quality && !a.MiscFlags && !a.pPrimaryDesc);
    const auto &m = a.pMipInfoList[0];
    require(m.TexelWidth && m.TexelWidth <= 16 * 1024 * 1024 && m.TexelHeight == 1 && m.TexelDepth == 1);
    const bool constant = a.BindFlags == D3D10_DDI_BIND_CONSTANT_BUFFER;
    const UINT geometry_flags = D3D10_DDI_BIND_VERTEX_BUFFER | D3D10_DDI_BIND_INDEX_BUFFER | D3D10_DDI_BIND_SHADER_RESOURCE;
    const bool geometry = a.BindFlags && !(a.BindFlags & ~geometry_flags);
    if (constant) require(m.TexelWidth <= 65536 && !(m.TexelWidth % 16));
    const bool staging = a.Usage == D3D10_DDI_USAGE_STAGING && !a.BindFlags && a.MapFlags == D3D10_DDI_CPU_ACCESS_READ;
    const bool ordinary = a.Usage == D3D10_DDI_USAGE_DEFAULT && (constant || geometry) && !a.MapFlags;
    const bool immutable = a.Usage == D3D10_DDI_USAGE_IMMUTABLE && (constant || geometry) && !a.MapFlags && a.pInitialDataUP;
    const bool dynamic = a.Usage == D3D10_DDI_USAGE_DYNAMIC && (constant || geometry) && a.MapFlags == D3D10_DDI_CPU_ACCESS_WRITE;
    require((staging || ordinary || immutable || dynamic) && (!a.pInitialDataUP || !staging));
    auto allocation = std::make_shared<Allocation>(d.context);
    // Reuse the private kernel storage contract. Logical buffer bytes are kept
    // separately; no texture view is admitted for this backing. At >32 KiB the
    // complete storage rows have no gaps, so logical bytes remain contiguous.
    const UINT width = (std::min)((m.TexelWidth + 3) / 4, 8192u);
    allocation->create(width, (m.TexelWidth + width * 4 - 1) / (width * 4), 28);
    r.allocation = std::move(allocation); r.usage = a.Usage; r.map_flags = a.MapFlags;
    r.buffer_bytes = m.TexelWidth; r.bind_flags = a.BindFlags;resource_boundary("buffer_created",r.allocation.get(),0,r.buffer_bytes);
    try { if (a.pInitialDataUP) update_buffer(r, a.pInitialDataUP->pSysMem); }
    catch (...) { r.allocation.reset(); r.buffer_bytes = 0; throw; }
}
void APIENTRY set_vertex_buffers(D3D10DDI_HDEVICE h, UINT start, UINT count,
    const D3D10DDI_HRESOURCE *values, const UINT *strides, const UINT *offsets) {
    guard(h, __FUNCTION__, [&] {
        require(start <= 16 && count <= 16 - start && (!count || (values && strides && offsets)));
        // This callback records state and reads no vertex memory. DWM binds
        // dynamic buffers before unmapping them. Fetch/draw still rejects a
        // mapped allocation before any provider work or target modification.
        auto next = device(h).vertex_buffers;
        for (UINT i = 0; i < count; ++i) {
            auto &binding = next[start + i]; binding = {};
            if (!values[i].pDrvPrivate) continue;
            auto &r = resource(values[i]);
            const bool valid=r.buffer_bytes && (r.bind_flags & D3D10_DDI_BIND_VERTEX_BUFFER) &&
                r.allocation->context == device(h).context &&
                strides[i] <= 2048 && offsets[i] <= r.buffer_bytes;
            if(!valid)lab_trace("{\"event\":\"vertex_binding_rejected\",\"start\":%u,\"count\":%u,\"slot\":%u,\"bytes\":%u,\"bind_flags\":%u,\"stride\":%u,\"offset\":%u,\"locked\":%s,\"same_context\":%s}\n",
                start,count,start+i,r.buffer_bytes,r.bind_flags,strides[i],offsets[i],r.allocation->locked?"true":"false",r.allocation->context==device(h).context?"true":"false");
            require(valid);
            binding.allocation = r.allocation; binding.bytes = r.buffer_bytes;
            binding.stride = strides[i]; binding.offset = offsets[i];
        }
        device(h).vertex_buffers = std::move(next);
    });
}
void APIENTRY set_index_buffer(D3D10DDI_HDEVICE h, D3D10DDI_HRESOURCE value, DXGI_FORMAT format, UINT offset) {
    guard(h, __FUNCTION__, [&] {
        if (!value.pDrvPrivate) { device(h).index_buffer = {}; return; }
        auto &r = resource(value);
        const UINT stride = format == DXGI_FORMAT_R16_UINT ? 2 : (format == DXGI_FORMAT_R32_UINT ? 4 : 0);
        require(stride && r.buffer_bytes && (r.bind_flags & D3D10_DDI_BIND_INDEX_BUFFER) &&
            offset <= r.buffer_bytes && !(offset % stride) &&
            r.allocation->context == device(h).context && !r.allocation->locked);
        device(h).index_buffer = {r.allocation, r.buffer_bytes, stride, offset, format};
    });
}
template<UINT Stage>
void APIENTRY set_constant_buffers(D3D10DDI_HDEVICE h, UINT start, UINT count, const D3D10DDI_HRESOURCE *values) {
    guard(h, __FUNCTION__, [&] {
        require(start <= 14 && count <= 14 - start && (!count || values));
        auto next = device(h).constant_buffers[Stage];
        auto sizes = device(h).constant_bytes[Stage];
        for (UINT i = 0; i < count; ++i) {
            if (!values[i].pDrvPrivate) { next[start + i].reset(); sizes[start + i] = 0; continue; }
            auto &r = resource(values[i]);
            require(r.buffer_bytes && r.bind_flags == D3D10_DDI_BIND_CONSTANT_BUFFER &&
                r.allocation->context == device(h).context && !r.allocation->locked);
            next[start + i] = r.allocation;
            sizes[start + i] = r.buffer_bytes;
        }
        device(h).constant_buffers[Stage] = std::move(next);
        device(h).constant_bytes[Stage] = sizes;
    });
}
