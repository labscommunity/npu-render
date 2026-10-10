// Included after Resource/Device definitions. CPU uploads into real VidMm backing.
void update_buffer(Resource &r, const void *source) {
    require(r.buffer_bytes && source && uintptr_t(source) <= UINTPTR_MAX - r.buffer_bytes &&
            !r.allocation->locked && !r.allocation->context->poisoned);
    Mapping write(*r.allocation, false);
    std::memcpy(write.data, source, r.buffer_bytes);
    write.finish(); counters.upload_bytes += r.buffer_bytes;
}
void create_buffer(Device &d, const D3D10DDIARG_CREATERESOURCE &a, Resource &r) {
    require(a.MipLevels == 1 && a.ArraySize == 1 && a.pMipInfoList && a.Format == DXGI_FORMAT_UNKNOWN &&
            a.SampleDesc.Count == 1 && !a.SampleDesc.Quality && !a.MiscFlags && !a.pPrimaryDesc);
    const auto &m = a.pMipInfoList[0];
    require(m.TexelWidth && m.TexelWidth <= 65536 && !(m.TexelWidth % 16) && m.TexelHeight == 1 && m.TexelDepth == 1);
    const bool staging = a.Usage == D3D10_DDI_USAGE_STAGING && !a.BindFlags && a.MapFlags == D3D10_DDI_CPU_ACCESS_READ;
    const bool ordinary = a.Usage == D3D10_DDI_USAGE_DEFAULT && a.BindFlags == D3D10_DDI_BIND_CONSTANT_BUFFER && !a.MapFlags;
    const bool dynamic = a.Usage == D3D10_DDI_USAGE_DYNAMIC && a.BindFlags == D3D10_DDI_BIND_CONSTANT_BUFFER && a.MapFlags == D3D10_DDI_CPU_ACCESS_WRITE;
    require((staging || ordinary || dynamic) && (!a.pInitialDataUP || !staging));
    auto allocation = std::make_shared<Allocation>(d.context);
    // Reuse the private kernel storage contract. Logical buffer bytes are kept
    // separately; no texture view is admitted for this backing. At >32 KiB the
    // two rows have no intervening padding, so logical bytes remain contiguous.
    const UINT width = (std::min)(m.TexelWidth / 4, 8192u);
    allocation->create(width, (m.TexelWidth + width * 4 - 1) / (width * 4), 28);
    r.allocation = std::move(allocation); r.usage = a.Usage; r.map_flags = a.MapFlags;
    r.buffer_bytes = m.TexelWidth; r.bind_flags = a.BindFlags;
    try { if (a.pInitialDataUP) update_buffer(r, a.pInitialDataUP->pSysMem); }
    catch (...) { r.allocation.reset(); r.buffer_bytes = 0; throw; }
}
template<UINT Stage>
void APIENTRY set_constant_buffers(D3D10DDI_HDEVICE h, UINT start, UINT count, const D3D10DDI_HRESOURCE *values) {
    guard(h, __FUNCTION__, [&] {
        require(start <= 14 && count <= 14 - start && (!count || values));
        auto next = device(h).constant_buffers[Stage];
        for (UINT i = 0; i < count; ++i) {
            if (!values[i].pDrvPrivate) { next[start + i].reset(); continue; }
            auto &r = resource(values[i]);
            require(r.buffer_bytes && r.bind_flags == D3D10_DDI_BIND_CONSTANT_BUFFER &&
                r.allocation->context == device(h).context && !r.allocation->locked);
            next[start + i] = r.allocation;
        }
        device(h).constant_buffers[Stage] = std::move(next);
    });
}
