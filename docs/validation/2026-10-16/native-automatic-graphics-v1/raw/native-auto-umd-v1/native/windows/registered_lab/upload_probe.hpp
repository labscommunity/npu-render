void exercise_upload(ID3D11Device *device, ID3D11DeviceContext *context, DXGI_FORMAT format, UINT sharing) {
    constexpr UINT width = 65, height = 37, pitch = width * 4 + 19;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = sharing == 2 ? D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX :
                     (sharing == 1 ? D3D11_RESOURCE_MISC_SHARED : 0);
    std::vector<unsigned char> input(pitch * height), expected(width * height * 4);
    for (UINT i = 0; i < input.size(); ++i) input[i] = static_cast<unsigned char>(i * 73 + 11);
    for (UINT y = 0; y < height; ++y)
        std::memcpy(expected.data() + y * width * 4, input.data() + y * pitch, width * 4);
    D3D11_SUBRESOURCE_DATA initial{input.data(), pitch, 0};
    ComPtr<ID3D11Texture2D> target, staging;
    check(device->CreateTexture2D(&desc, &initial, &target), "create_upload_target");
    std::fill(input.begin(), input.end(), static_cast<unsigned char>(0x98)); // Caller storage must no longer be retained.
    ComPtr<IDXGIKeyedMutex> keyed;
    if (sharing == 2) check(target.As(&keyed), "upload_keyed_mutex");
    KeyedAccess access(keyed.Get(), 0);
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = desc.MiscFlags = 0;
    check(device->CreateTexture2D(&desc, nullptr, &staging), "create_upload_staging");
    for (UINT pass = 0; pass < 4; ++pass) {
        if (pass == 1) {
            for (UINT i = 0; i < input.size(); ++i) input[i] = static_cast<unsigned char>(i * 31 + 19);
            for (UINT y = 0; y < height; ++y)
                std::memcpy(expected.data() + y * width * 4, input.data() + y * pitch, width * 4);
            context->UpdateSubresource(target.Get(), 0, nullptr, input.data(), pitch, 0);
        } else if (pass == 2) {
            const D3D11_BOX box{3, 4, 0, 16, 17, 1};
            for (UINT i = 0; i < input.size(); ++i) input[i] = static_cast<unsigned char>(i * 11 + 237);
            for (UINT y = box.top; y < box.bottom; ++y)
                std::memcpy(expected.data() + (y * width + box.left) * 4,
                            input.data() + (y - box.top) * pitch, (box.right - box.left) * 4);
            context->UpdateSubresource(target.Get(), 0, &box, input.data(), pitch, 0);
        } else if (pass == 3) {
            const D3D11_BOX empty{3, 4, 0, 3, 17, 1};
            context->UpdateSubresource(target.Get(), 0, &empty, input.data(), pitch, 0);
        }
        check(device->GetDeviceRemovedReason(), "upload_device_status");
        std::fill(input.begin(), input.end(), static_cast<unsigned char>(0x98));
        context->CopyResource(staging.Get(), target.Get());
        check(device->GetDeviceRemovedReason(), "upload_readback_status");
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "map_upload_readback");
        bool valid = mapped.pData && mapped.RowPitch >= width * 4;
        for (UINT y = 0; valid && y < height; ++y)
            valid = std::memcmp(static_cast<const unsigned char *>(mapped.pData) + size_t(y) * mapped.RowPitch,
                                expected.data() + y * width * 4, width * 4) == 0;
        context->Unmap(staging.Get(), 0);
        check(device->GetDeviceRemovedReason(), "unmap_upload_readback_status");
        require(valid, "uploaded pixel mismatch");
        std::printf("{\"upload_format\":%u,\"sharing\":%u,\"pass\":%u,\"width\":65,\"height\":37,\"verified_bytes\":9620}\n",
                    UINT(format), sharing, pass);
    }
    access.release(0);
    context->Flush();
}
