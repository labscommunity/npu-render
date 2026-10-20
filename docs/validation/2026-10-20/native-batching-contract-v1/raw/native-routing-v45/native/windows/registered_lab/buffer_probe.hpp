// Ordinary D3D11 buffers on the registered adapter. This performs no NPU work.
void exercise_buffers(ID3D11Device *device, ID3D11DeviceContext *context) {
    for (const UINT bytes : {16u, 272u, 32768u, 32784u, 65536u}) {
        std::vector<unsigned char> expected(bytes);
        for (UINT i = 0; i < bytes; ++i) expected[i] = static_cast<unsigned char>(i * 37 + bytes / 16);
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth = bytes; desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = expected.data();
        ComPtr<ID3D11Buffer> target, dynamic, staging;
        check(device->CreateBuffer(&desc, &initial, &target), "create_constant_buffer");
        desc.Usage = D3D11_USAGE_DYNAMIC; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&desc, nullptr, &dynamic), "create_dynamic_constant_buffer");
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateBuffer(&desc, nullptr, &staging), "create_buffer_staging");
        auto binding = target.Get();
        context->VSSetConstantBuffers(0, 1, &binding);
        context->PSSetConstantBuffers(2, 1, &binding);
        check(device->GetDeviceRemovedReason(), "constant_bindings");
        for (UINT pass = 0; pass < 4; ++pass) {
            ID3D11Buffer *source = target.Get();
            if (pass) {
                for (UINT i = 0; i < bytes; ++i) expected[i] = static_cast<unsigned char>(i * 73 + pass * 29);
                if (pass == 1) context->UpdateSubresource(target.Get(), 0, nullptr, expected.data(), 0, 0);
                else {
                    source = dynamic.Get();
                    D3D11_MAPPED_SUBRESOURCE write{};
                    check(context->Map(source, 0, D3D11_MAP_WRITE_DISCARD, 0, &write), "map_constant_discard");
                    require(write.pData != nullptr, "constant write mapping absent");
                    std::memcpy(write.pData, expected.data(), bytes);
                    context->Unmap(source, 0);
                }
            }
            check(device->GetDeviceRemovedReason(), "buffer_update_status");
            context->CopyResource(staging.Get(), source);
            check(device->GetDeviceRemovedReason(), "buffer_copy_status");
            D3D11_MAPPED_SUBRESOURCE read{};
            check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &read), "map_buffer_readback");
            const bool valid = read.pData && !std::memcmp(read.pData, expected.data(), bytes);
            context->Unmap(staging.Get(), 0);
            require(valid, "constant buffer byte oracle failed");
            std::printf("{\"buffer_bytes\":%u,\"pass\":%u,\"readback_verified\":true,\"npu_execution_tested\":false}\n", bytes, pass);
        }
        binding = nullptr;
        context->VSSetConstantBuffers(0, 1, &binding);
        context->PSSetConstantBuffers(2, 1, &binding);
    }
}
