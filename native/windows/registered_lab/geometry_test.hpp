// Resource/IA ownership checks with callback shims; no vertex shader execution.
int test_half_input(D3D10DDI_HDEVICE h, const D3D10DDI_DEVICEFUNCS &f, D3D10DDI_HRESOURCE buffer) {
    // Exhaustive conversion against an independent arithmetic oracle.
    for (uint32_t raw = 0; raw < 65536; ++raw) {
        const auto bits = half_to_float_bits(uint16_t(raw)); float actual; std::memcpy(&actual, &bits, 4);
        const auto exponent = (raw >> 10) & 31, fraction = raw & 1023;
        TEST(std::signbit(actual) == bool(raw & 0x8000));
        if (exponent == 31) { TEST(fraction ? std::isnan(actual) : std::isinf(actual)); }
        else {
            float expected = std::ldexp(float(fraction + (exponent ? 1024 : 0)), exponent ? int(exponent) - 25 : -24);
            if (raw & 0x8000) expected = -expected;
            TEST(actual == expected);
        }
    }
    const uint16_t patterns[]{0x0000, 0x8000, 0x0001, 0x03ff, 0x0400, 0x3c00, 0xbc00, 0x7bff, 0x7c00, 0xfc00, 0x7e01, 0xfe01};
    D3D10DDI_MAPPED_SUBRESOURCE mapped{};
    f.pfnDynamicIABufferMapNoOverwrite(h, buffer, 0, D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0, &mapped);
    TEST(last_error == S_OK && mapped.pData);
    for (UINT i = 0; i < 12; ++i) {
        const uint16_t data[]{patterns[i], patterns[(i + 1) % 12], patterns[(i + 2) % 12], patterns[(i + 3) % 12]};
        std::memcpy(static_cast<uint8_t *>(mapped.pData) + 2 + i * 10, data, sizeof(data));
    }
    f.pfnDynamicIABufferUnmap(h, buffer, 0);
    const UINT stride = 10, offset = 0; f.pfnIaSetVertexBuffers(h, 3, 1, &buffer, &stride, &offset);
    for (const auto format : {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT}) {
        D3D10DDIARG_INPUT_ELEMENT_DESC element{3, 2, format, D3D10_DDI_INPUT_PER_VERTEX_DATA, 0, 3};
        D3D10DDIARG_CREATEELEMENTLAYOUT args{&element, 1};
        alignas(ElementLayout) unsigned char storage[sizeof(ElementLayout)]{};
        D3D10DDI_HELEMENTLAYOUT layout{}; layout.pDrvPrivate = storage;
        f.pfnCreateElementLayout(h, &args, layout, {}); f.pfnIaSetInputLayout(h, layout);
        TEST(last_error == S_OK);
        f.pfnDestroyElementLayout(h, layout);
        const auto vertices = fetch_vertices(device(h), 12, 0);
        const auto lanes = element_format(format).lanes;
        for (UINT i = 0; i < 12; ++i) for (UINT c = 0; c < 4; ++c)
            TEST(vertices[i][3][c] == (c < lanes ? half_to_float_bits(patterns[(i + c) % 12]) : (c == 3 ? 0x3f800000u : 0u)));
        element.AlignedByteOffset = 3; f.pfnCreateElementLayout(h, &args, layout, {});
        TEST(last_error == E_INVALIDARG); f.pfnDestroyElementLayout(h, layout); last_error = S_OK;
        f.pfnIaSetInputLayout(h, {});
    }
    std::puts("{\"half_input_decoder\":true,\"encodings_checked\":65536,\"formats_checked\":3,\"host_decode_only\":true}");
    return 0;
}
int test_integer_input(D3D10DDI_HDEVICE h, const D3D10DDI_DEVICEFUNCS &f, D3D10DDI_HRESOURCE buffer) {
    const DXGI_FORMAT formats[]{DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8B8A8_UINT,
        DXGI_FORMAT_R8_SINT, DXGI_FORMAT_R8G8_SINT, DXGI_FORMAT_R8G8B8A8_SINT,
        DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16B16A16_UINT,
        DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R16G16B16A16_SINT};
    uint64_t checked = 0;
    for (const auto id : formats) {
        const auto format = element_format(id);
        const uint32_t range = 1u << (format.component_bytes * 8);
        for (uint32_t raw = 0; raw < range; ++raw) {
            uint8_t bytes[9]{};
            for (UINT c = 0; c < format.lanes; ++c) {
                const uint32_t x = (raw + c * 73) % range;
                std::memcpy(bytes + 1 + c * format.component_bytes, &x, format.component_bytes);
            }
            nr::shader::VertexBits actual{}; decode_element(actual, bytes + 1, format);
            for (UINT c = 0; c < 4; ++c) {
                const int64_t x = (raw + c * 73) % range;
                const auto signed_value = format.signed_integer && x >= range / 2 ? x - range : x;
                const uint32_t expected = c < format.lanes ? uint32_t(signed_value) : UINT(c == 3);
                TEST(actual[c] == expected); ++checked;
            }
        }
        D3D10DDI_MAPPED_SUBRESOURCE mapped{};
        f.pfnDynamicIABufferMapDiscard(h, buffer, 0, D3D10_DDI_MAP_WRITE_DISCARD, 0, &mapped);
        TEST(last_error == S_OK && mapped.pData);
        const UINT stride = 12, offset = format.component_bytes;
        const uint32_t values[]{0, range / 2 - 1, range / 2, range - 1};
        for (UINT i = 0; i < 3; ++i) for (UINT c = 0; c < format.lanes; ++c)
            std::memcpy(static_cast<uint8_t *>(mapped.pData) + offset + i * stride + c * format.component_bytes,
                &values[(i+c)%4], format.component_bytes);
        f.pfnDynamicIABufferUnmap(h, buffer, 0);
        f.pfnIaSetVertexBuffers(h, 3, 1, &buffer, &stride, &offset);
        D3D10DDIARG_INPUT_ELEMENT_DESC element{3, 0, id, D3D10_DDI_INPUT_PER_VERTEX_DATA, 0, 1};
        D3D10DDIARG_CREATEELEMENTLAYOUT args{&element, 1};
        alignas(ElementLayout) unsigned char storage[sizeof(ElementLayout)]{};
        D3D10DDI_HELEMENTLAYOUT layout{}; layout.pDrvPrivate = storage;
        f.pfnCreateElementLayout(h, &args, layout, {}); f.pfnIaSetInputLayout(h, layout);
        TEST(last_error == S_OK); f.pfnDestroyElementLayout(h, layout);
        const auto consecutive = fetch_vertices(device(h), 3, 0);
        const auto reordered = fetch_vertices_at(device(h), {2,0,1}, {9,8,7});
        TEST(reordered[0] == consecutive[2] && reordered[1] == consecutive[0] && reordered[2] == consecutive[1]);
        for (UINT i = 0; i < 3; ++i) for (UINT c = 0; c < 4; ++c) {
            const int64_t x = values[(i+c)%4];
            const auto signed_value = format.signed_integer && x >= range / 2 ? x - range : x;
            TEST(consecutive[i][1][c] == (c < format.lanes ? uint32_t(signed_value) : UINT(c == 3)));
        }
        if (format.component_bytes == 2) {
            element.AlignedByteOffset = 1; f.pfnCreateElementLayout(h, &args, layout, {});
            TEST(last_error == E_INVALIDARG); f.pfnDestroyElementLayout(h, layout); last_error = S_OK;
        }
        f.pfnIaSetInputLayout(h, {});
    }
    std::printf("{\"integer_input_decoder\":true,\"formats_checked\":12,\"components_checked\":%llu,\"host_decode_only\":true}\n", static_cast<unsigned long long>(checked));
    return 0;
}
int test_instance_input(D3D10DDI_HDEVICE h,const D3D10DDI_DEVICEFUNCS &f,D3D10DDI_HRESOURCE buffer)try{
    D3D10DDI_MAPPED_SUBRESOURCE mapped{};
    f.pfnDynamicIABufferMapDiscard(h,buffer,0,D3D10_DDI_MAP_WRITE_DISCARD,0,&mapped);TEST(last_error==S_OK && mapped.pData);
    for(UINT i=0;i<16;++i){const uint32_t value=0x81230000u+i;std::memcpy(static_cast<uint8_t *>(mapped.pData)+4+i*8,&value,4);}
    f.pfnDynamicIABufferUnmap(h,buffer,0);const UINT stride=8,offset=4;f.pfnIaSetVertexBuffers(h,3,1,&buffer,&stride,&offset);
    const auto previous=device(h).vertex_program;
    const auto tokens=nr::registered_lab::shader_tokens("float4 main(uint a:A,uint v:SV_VertexID,uint i:SV_InstanceID):SV_Position{return asfloat(uint4(a,v,i,0x3f800000));}","vs_4_0");
    auto program=std::make_shared<nr::shader::Program>(nr::shader::parse_vertex(tokens.data(),tokens.size()));
    TEST(program->vertex_id_input<16 && program->instance_id_input<16);
    // The semantic register for A is the remaining declared input.
    UINT attribute=16;for(UINT r=0;r<16;++r)if(program->inputs[r] && r!=program->vertex_id_input && r!=program->instance_id_input)attribute=r;
    TEST(attribute<16);device(h).vertex_program=program;
    for(UINT rate:{0u,1u,2u,3u}){
        D3D10DDIARG_INPUT_ELEMENT_DESC element{3,0,DXGI_FORMAT_R32_UINT,D3D10_DDI_INPUT_PER_INSTANCE_DATA,rate,attribute};
        D3D10DDIARG_CREATEELEMENTLAYOUT args{&element,1};alignas(ElementLayout) unsigned char storage[sizeof(ElementLayout)]{};
        D3D10DDI_HELEMENTLAYOUT layout{};layout.pDrvPrivate=storage;f.pfnCreateElementLayout(h,&args,layout,{});f.pfnIaSetInputLayout(h,layout);TEST(last_error==S_OK);f.pfnDestroyElementLayout(h,layout);
        const auto data=fetch_vertices_at(device(h),{5,2,9},{10,7,14},5,3);
        TEST(data.size()==15);
        for(UINT n=0;n<15;++n){const auto instance=n/3;const UINT ids[]{10,7,14};
            TEST(data[n][attribute][0]==0x81230003u+(rate?instance/rate:0));
            TEST(data[n][program->vertex_id_input][0]==ids[n%3] && data[n][program->instance_id_input][0]==instance);
        }
        bool rejected=false;try{(void)fetch_vertices_at(device(h),{0},{0},2,UINT_MAX);}catch(...){rejected=true;}TEST(rejected);
        f.pfnIaSetInputLayout(h,{});
    }
    device(h).vertex_program=previous;
    std::puts("{\"instanced_input_fetch\":true,\"step_rates\":[0,1,2,3],\"nonzero_start_instance\":true,\"system_ids_exact\":true}");return 0;
}catch(const std::exception &e){std::fprintf(stderr,"instance fixture: %s\n",e.what());return 1;}
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
            TEST(test_half_input(h, f, value) == 0);
            TEST(test_integer_input(h, f, value) == 0);
            TEST(test_instance_input(h,f,value)==0);
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
