// Geometry-buffer storage and bindings only: no draw or NPU claim.
void exercise_geometry_buffers(ID3D11Device *device, ID3D11DeviceContext *context) {
    for (const UINT bind : {UINT(D3D11_BIND_VERTEX_BUFFER), UINT(D3D11_BIND_INDEX_BUFFER), 3u})
    for (const UINT bytes : {7u, 272u, 32769u, 65540u})
    for (const auto usage : {D3D11_USAGE_DEFAULT, D3D11_USAGE_DYNAMIC, D3D11_USAGE_IMMUTABLE}) {
        std::vector<unsigned char> expected(bytes);
        for (UINT i = 0; i < bytes; ++i) expected[i] = static_cast<unsigned char>(i * 43 + bytes);
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth = bytes; desc.BindFlags = bind; desc.Usage = usage;
        if (usage == D3D11_USAGE_DYNAMIC) desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = expected.data();
        ComPtr<ID3D11Buffer> target, staging;
        check(device->CreateBuffer(&desc, &initial, &target), "create_geometry_buffer");
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateBuffer(&desc, nullptr, &staging), "create_geometry_staging");
        auto value = target.Get(); const UINT stride = 20, offset = 0;
        if (bind & D3D11_BIND_VERTEX_BUFFER) context->IASetVertexBuffers(3, 1, &value, &stride, &offset);
        if (bind & D3D11_BIND_INDEX_BUFFER) context->IASetIndexBuffer(value, DXGI_FORMAT_R16_UINT, 0);
        check(device->GetDeviceRemovedReason(), "geometry_bindings");
        const UINT passes = usage == D3D11_USAGE_DYNAMIC ? 3 : (usage == D3D11_USAGE_DEFAULT ? 2 : 1);
        for (UINT pass = 0; pass < passes; ++pass) {
            if (pass) {
                if (usage == D3D11_USAGE_DEFAULT) {
                    for (UINT i = 0; i < bytes; ++i) expected[i] = static_cast<unsigned char>(i * 29 + pass);
                    context->UpdateSubresource(value, 0, nullptr, expected.data(), 0, 0);
                } else {
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    const auto type = pass == 1 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
                    check(context->Map(value, 0, type, 0, &mapped), "map_geometry_write");
                    require(mapped.pData != nullptr, "geometry mapping absent");
                    // Every previous copy/readback has completed before NO_OVERWRITE.
                    if (pass == 1) {
                        for (UINT i = 0; i < bytes; ++i) expected[i] = static_cast<unsigned char>(i * 31 + 11);
                        std::memcpy(mapped.pData, expected.data(), bytes);
                    } else {
                        expected.back() ^= 0xe3;
                        static_cast<unsigned char *>(mapped.pData)[bytes - 1] = expected.back();
                    }
                    context->Unmap(value, 0);
                }
            }
            context->CopyResource(staging.Get(), value);
            check(device->GetDeviceRemovedReason(), "geometry_copy_status");
            D3D11_MAPPED_SUBRESOURCE read{};
            check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &read), "map_geometry_readback");
            const bool valid = read.pData && !std::memcmp(read.pData, expected.data(), bytes);
            context->Unmap(staging.Get(), 0);
            require(valid, "geometry byte oracle failed");
        }
        value = nullptr;
        if (bind & D3D11_BIND_VERTEX_BUFFER) context->IASetVertexBuffers(3, 1, &value, &stride, &offset);
        if (bind & D3D11_BIND_INDEX_BUFFER) context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        std::printf("{\"geometry_bytes\":%u,\"bind\":%u,\"usage\":%u,\"verified_readbacks\":%u,\"npu_execution_tested\":false}\n", bytes, bind, UINT(usage), passes);
    }
}
