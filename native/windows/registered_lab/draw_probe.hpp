void exercise_draw(ID3D11Device *device, ID3D11DeviceContext *context, DXGI_FORMAT format, UINT width, UINT sharing) {
    constexpr UINT height = 16;
    std::vector<unsigned char> foreground(size_t(width) * height * 4), background(foreground.size());
    for (size_t i = 0; i < foreground.size(); i += 4) {
        const auto alpha = static_cast<unsigned char>((i / 4 * 37) % 256);
        for (size_t c = 0; c < 3; ++c) {
            foreground[i + c] = static_cast<unsigned char>((i / 4 + c * 23) % (UINT(alpha) + 1));
            background[i + c] = static_cast<unsigned char>((i / 4 + c * 53) % 256);
        }
        foreground[i + 3] = alpha; background[i + 3] = 255;
    }
    auto vertex_code = nr::registered_lab::compile_shader(nr_ddi_vertex_shader, "vs_4_0");
    auto pixel_code = nr::registered_lab::compile_shader(nr_ddi_pixel_shader, "ps_4_0");
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    check(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &vertex), "create_vertex_shader");
    check(device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(), nullptr, &pixel), "create_pixel_shader");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1; desc.Format = format;
    desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = sharing == 2 ? D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX :
                     (sharing == 1 ? D3D11_RESOURCE_MISC_SHARED : 0);
    D3D11_SUBRESOURCE_DATA initial{background.data(), width * 4, 0};
    ComPtr<ID3D11Texture2D> target, source, staging;
    check(device->CreateTexture2D(&desc, &initial, &target), "create_draw_target");
    initial.pSysMem = foreground.data(); desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    check(device->CreateTexture2D(&desc, &initial, &source), "create_draw_source");
    ComPtr<IDXGIKeyedMutex> target_mutex, source_mutex;
    if (sharing == 2) {
        check(target.As(&target_mutex), "draw_target_mutex"); check(source.As(&source_mutex), "draw_source_mutex");
    }
    KeyedAccess target_access(target_mutex.Get(), 0), source_access(source_mutex.Get(), 0);
    desc.BindFlags = desc.MiscFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&desc, nullptr, &staging), "create_draw_staging");
    ComPtr<ID3D11RenderTargetView> tv;
    ComPtr<ID3D11ShaderResourceView> sv;
    check(device->CreateRenderTargetView(target.Get(), nullptr, &tv), "create_draw_rtv");
    check(device->CreateShaderResourceView(source.Get(), nullptr, &sv), "create_draw_srv");
    D3D11_DEPTH_STENCIL_DESC depth_desc{};
    depth_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ComPtr<ID3D11DepthStencilState> depth;
    check(device->CreateDepthStencilState(&depth_desc, &depth), "create_draw_depth");
    D3D11_RASTERIZER_DESC raster_desc{};
    raster_desc.FillMode = D3D11_FILL_SOLID; raster_desc.CullMode = D3D11_CULL_NONE; raster_desc.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    check(device->CreateRasterizerState(&raster_desc, &raster), "create_draw_raster");
    D3D11_BLEND_DESC blend_desc{};
    auto &b = blend_desc.RenderTarget[0];
    b.BlendEnable = TRUE; b.SrcBlend = b.SrcBlendAlpha = D3D11_BLEND_ONE;
    b.DestBlend = b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD; b.RenderTargetWriteMask = 15;
    ComPtr<ID3D11BlendState> blend;
    check(device->CreateBlendState(&blend_desc, &blend), "create_draw_blend");
    auto target_view = tv.Get(); auto source_view = sv.Get();
    context->OMSetRenderTargets(1, &target_view, nullptr); context->PSSetShaderResources(0, 1, &source_view);
    context->VSSetShader(vertex.Get(), nullptr, 0); context->PSSetShader(pixel.Get(), nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->OMSetDepthStencilState(depth.Get(), 0); context->RSSetState(raster.Get());
    D3D11_VIEWPORT viewport{0, 0, FLOAT(width), FLOAT(height), 0, 1}; context->RSSetViewports(1, &viewport);
    for (UINT pass = 0; pass < 2; ++pass) {
        if (pass) {
            const D3D11_BOX box{width / 4, height / 4, 0, width / 4 + width / 2, height / 4 + height / 2, 1};
            for (UINT y = box.top; y < box.bottom; ++y) for (UINT x = box.left; x < box.right; ++x) {
                const auto i = (size_t(y) * width + x) * 4;
                foreground[i] = 23; foreground[i + 1] = 42; foreground[i + 2] = 17; foreground[i + 3] = 128;
            }
            context->UpdateSubresource(source.Get(), 0, &box, foreground.data() + (size_t(box.top) * width + box.left) * 4, width * 4, 0);
        }
        context->OMSetBlendState(pass ? nullptr : blend.Get(), nullptr, ~0u);
        context->Draw(3, 0);
        check(device->GetDeviceRemovedReason(), "draw_device_status");
        context->CopyResource(staging.Get(), target.Get());
        check(device->GetDeviceRemovedReason(), "draw_readback_status");
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "draw_map");
        require(mapped.pData && mapped.RowPitch >= width * 4, "draw mapping shape");
        UINT max_error = 0;
        for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) for (UINT c = 0; c < 4; ++c) {
            const auto i = (size_t(y) * width + x) * 4;
            const auto expected = pass ? foreground[i + c] : std::clamp<int>(int(std::lround(
                foreground[i + c] + background[i + c] * (255 - foreground[i + 3]) / 255.0)), 0, 255);
            const auto actual = static_cast<const unsigned char *>(mapped.pData)[size_t(y) * mapped.RowPitch + x * 4 + c];
            max_error = std::max(max_error, UINT(std::abs(int(actual) - expected)));
        }
        context->Unmap(staging.Get(), 0);
        check(device->GetDeviceRemovedReason(), "draw_unmap_status");
        require(max_error <= (pass ? 0u : 1u), "composition pixel mismatch");
        std::printf("{\"draw_format\":%u,\"width\":%u,\"height\":16,\"sharing\":%u,\"pass\":%u,\"verified_bytes\":%zu,\"max_error\":%u}\n",
                    UINT(format), width, sharing, pass, foreground.size(), max_error);
    }
    context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView *none = nullptr; context->PSSetShaderResources(0, 1, &none);
    context->VSSetShader(nullptr, nullptr, 0); context->PSSetShader(nullptr, nullptr, 0);
    context->OMSetBlendState(nullptr, nullptr, ~0u); context->OMSetDepthStencilState(nullptr, 0); context->RSSetState(nullptr);
    source_access.release(0); target_access.release(0);
    context->Flush();
}
