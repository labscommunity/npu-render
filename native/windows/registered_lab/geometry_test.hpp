// Resource/IA ownership checks with callback shims; no vertex shader execution.
int test_input_layout(D3D10DDI_HDEVICE h, const D3D10DDI_DEVICEFUNCS &f, D3D10DDI_HRESOURCE buffer) {
    D3D10DDI_MAPPED_SUBRESOURCE mapped{};
    f.pfnDynamicIABufferMapNoOverwrite(h, buffer, 0, D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0, &mapped);
    TEST(last_error == S_OK && mapped.pData);
    for (UINT i = 0; i < 2; ++i) {
        auto *data = static_cast<unsigned char *>(mapped.pData) + 4 + i * 20;
        const float position[]{float(i) + 0.25f, -0.5f, 0.125f};
        std::memcpy(data, position, sizeof(position));
        const unsigned char color[]{255, 0, 127, static_cast<unsigned char>(i * 255)};
        std::memcpy(data + 12, color, sizeof(color));
    }
    f.pfnDynamicIABufferUnmap(h, buffer, 0);
    D3D10DDIARG_INPUT_ELEMENT_DESC elements[]{
        {3, 0, DXGI_FORMAT_R32G32B32_FLOAT, D3D10_DDI_INPUT_PER_VERTEX_DATA, 0, 2},
        {3, 12, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_DDI_INPUT_PER_VERTEX_DATA, 0, 4}};
    D3D10DDIARG_CREATEELEMENTLAYOUT args{elements, 2};
    alignas(ElementLayout) unsigned char storage[sizeof(ElementLayout)]{}, bad_storage[sizeof(ElementLayout)]{};
    D3D10DDI_HELEMENTLAYOUT layout{}, invalid{}; layout.pDrvPrivate = storage; invalid.pDrvPrivate = bad_storage;
    TEST(f.pfnCalcPrivateElementLayoutSize(h, &args) == sizeof(ElementLayout));
    f.pfnCreateElementLayout(h, &args, layout, {}); f.pfnIaSetInputLayout(h, layout);
    TEST(last_error == S_OK && device(h).input_elements);
    const std::weak_ptr<const InputElements> retained = device(h).input_elements;
    f.pfnDestroyElementLayout(h, layout);
    TEST(!retained.expired());
    elements[1].InputRegister = 2; // Duplicate register and failed binding are transactional.
    f.pfnCreateElementLayout(h, &args, invalid, {});
    TEST(last_error == E_INVALIDARG);
    f.pfnIaSetInputLayout(h, invalid);
    TEST(last_error == E_INVALIDARG && device(h).input_elements == retained.lock());
    f.pfnDestroyElementLayout(h, invalid); last_error = S_OK;
    const auto fetched = fetch_vertices(device(h), 2, 0);
    for (UINT i = 0; i < 2; ++i) {
        const float position[]{float(i) + 0.25f, -0.5f, 0.125f, 1.0f};
        const float color[]{1.0f, 0.0f, 127.0f / 255.0f, float(i)};
        TEST(!std::memcmp(fetched[i][2].data(), position, sizeof(position)));
        TEST(!std::memcmp(fetched[i][4].data(), color, sizeof(color)));
        TEST(fetched[i][0] == nr::shader::VertexBits{});
    }
    TEST(fetch_vertices(device(h), 1, 1)[0] == fetched[1]);
    for (const auto count : {0u, 65536u, 65537u}) {
        bool rejected = false;
        try { (void)fetch_vertices(device(h), count, 0); }
        catch (const Error &) { rejected = true; }
        TEST(rejected && !resource(buffer).allocation->locked);
    }
    const UINT zero_stride = 0, offset = 4;
    f.pfnIaSetVertexBuffers(h, 3, 1, &buffer, &zero_stride, &offset);
    const auto broadcast = fetch_vertices(device(h), 2, 0xffffffffu - 1);
    TEST(broadcast[0] == fetched[0] && broadcast[1] == fetched[0]);
    f.pfnIaSetInputLayout(h, {});
    TEST(last_error == S_OK && retained.expired());
    return 0;
}
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
            TEST(test_input_layout(h, f, value) == 0);
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
