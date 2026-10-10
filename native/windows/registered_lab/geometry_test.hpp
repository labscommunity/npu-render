// Resource/IA ownership checks with callback shims; no vertex shader execution.
int test_geometry_buffers(D3D10DDI_HDEVICE h, const D3D10DDI_DEVICEFUNCS &f) {
    for (const UINT bind : {UINT(D3D10_DDI_BIND_VERTEX_BUFFER), UINT(D3D10_DDI_BIND_INDEX_BUFFER), 3u}) {
        alignas(Resource) unsigned char backing[sizeof(Resource)]{};
        D3D10DDI_HRESOURCE value{}; value.pDrvPrivate = backing;
        D3D10DDI_MIPINFO mip{}; mip.TexelWidth = 32769; mip.TexelHeight = mip.TexelDepth = 1;
        D3D10DDIARG_CREATERESOURCE a{}; a.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
        a.MipLevels = a.ArraySize = a.SampleDesc.Count = 1; a.pMipInfoList = &mip;
        a.Usage = D3D10_DDI_USAGE_DYNAMIC; a.BindFlags = bind; a.MapFlags = D3D10_DDI_CPU_ACCESS_WRITE;
        last_error = S_OK; f.pfnCreateResource(h, &a, value, {});
        TEST(last_error == S_OK && resource(value).buffer_bytes == 32769);
        D3D10DDI_MAPPED_SUBRESOURCE mapped{};
        f.pfnDynamicIABufferMapDiscard(h, value, 0, D3D10_DDI_MAP_WRITE_DISCARD, 0, &mapped);
        TEST(last_error == S_OK && mapped.pData); std::memset(mapped.pData, 0x35, 32769);
        f.pfnDynamicIABufferUnmap(h, value, 0);
        f.pfnDynamicIABufferMapNoOverwrite(h, value, 0, D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0, &mapped);
        TEST(last_error == S_OK && static_cast<unsigned char *>(mapped.pData)[32768] == 0x35);
        static_cast<unsigned char *>(mapped.pData)[32768] = 0x91;
        f.pfnDynamicIABufferUnmap(h, value, 0);
        const auto allocation = resource(value).allocation->handle;
        TEST(memory[allocation][32768] == 0x91);
        const UINT stride = 20, offset = 4;
        if (bind & D3D10_DDI_BIND_VERTEX_BUFFER) {
            f.pfnIaSetVertexBuffers(h, 3, 1, &value, &stride, &offset);
            TEST(last_error == S_OK && device(h).vertex_buffers[3].allocation == resource(value).allocation);
            const UINT invalid = 32770;
            f.pfnIaSetVertexBuffers(h, 3, 1, &value, &stride, &invalid);
            TEST(last_error == E_INVALIDARG && device(h).vertex_buffers[3].offset == 4);
            last_error = S_OK;
        }
        if (bind & D3D10_DDI_BIND_INDEX_BUFFER) {
            f.pfnIaSetIndexBuffer(h, value, DXGI_FORMAT_R16_UINT, 2);
            TEST(last_error == S_OK && device(h).index_buffer.allocation == resource(value).allocation);
            f.pfnIaSetIndexBuffer(h, value, DXGI_FORMAT_R16_UINT, 3);
            TEST(last_error == E_INVALIDARG && device(h).index_buffer.offset == 2);
            last_error = S_OK;
        }
        f.pfnDestroyResource(h, value);
        TEST(counters.live_allocations == 1); // Bound state owns the backing.
        const D3D10DDI_HRESOURCE empty{};
        f.pfnIaSetVertexBuffers(h, 3, 1, &empty, &stride, &offset);
        f.pfnIaSetIndexBuffer(h, empty, DXGI_FORMAT_UNKNOWN, 0);
        TEST(last_error == S_OK && !counters.live_allocations && memory[allocation].empty());
    }
    return 0;
}
