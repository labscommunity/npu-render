// Test workload: the admitted texture shader blends a spatial alpha pattern
// into the real swap-chain back buffer. The screen oracle is independent.
struct PresentDraw {
    ComPtr<ID3D11Texture2D> source;
    ComPtr<ID3D11ShaderResourceView> source_view;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    void prepare(ID3D11Device *device, UINT width, UINT height) {
        auto vc = nr::registered_lab::compile_shader(nr_ddi_vertex_shader, "vs_4_0");
        auto pc = nr::registered_lab::compile_shader(nr_ddi_pixel_shader, "ps_4_0");
        check(device->CreateVertexShader(vc->GetBufferPointer(), vc->GetBufferSize(), nullptr, &vertex), "present_vertex_shader");
        check(device->CreatePixelShader(pc->GetBufferPointer(), pc->GetBufferSize(), nullptr, &pixel), "present_pixel_shader");
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = width; texture.Height = height; texture.MipLevels = texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM; texture.SampleDesc.Count = 1;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&texture, nullptr, &source), "present_draw_source");
        check(device->CreateShaderResourceView(source.Get(), nullptr, &source_view), "present_draw_source_view");
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        check(device->CreateDepthStencilState(&dd, &depth), "present_draw_depth");
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&rd, &raster), "present_draw_raster");
        D3D11_BLEND_DESC bd{}; auto &b = bd.RenderTarget[0];
        b.BlendEnable = TRUE; b.SrcBlend = b.SrcBlendAlpha = D3D11_BLEND_ONE;
        b.DestBlend = b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD; b.RenderTargetWriteMask = 15;
        check(device->CreateBlendState(&bd, &blend), "present_draw_blend");
    }
    void render(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
                UINT width, UINT height, UINT pass, std::vector<unsigned char> &expected) {
        std::vector<unsigned char> foreground(size_t(width) * height * 4);
        for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
            const auto i = (size_t(y) * width + x) * 4;
            const UINT alpha = (x * 13 + y * 17 + pass * 31) % 256;
            for (UINT c = 0; c < 3; ++c) {
                foreground[i + c] = static_cast<unsigned char>((x * 7 + y * 11 + c * 29 + pass * 19) % (alpha + 1));
                // The background is an opaque RGB primary, so this reference
                // blend is exact integer arithmetic, independent of OpenVINO.
                expected[i + c] = static_cast<unsigned char>(foreground[i + c] + (c == 2 - pass ? 255 - alpha : 0));
            }
            foreground[i + 3] = static_cast<unsigned char>(alpha); expected[i + 3] = 255;
        }
        context->UpdateSubresource(source.Get(), 0, nullptr, foreground.data(), width * 4, 0);
        auto sv = source_view.Get();
        context->OMSetRenderTargets(1, &target, nullptr); context->PSSetShaderResources(0, 1, &sv);
        context->VSSetShader(vertex.Get(), nullptr, 0); context->PSSetShader(pixel.Get(), nullptr, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->OMSetDepthStencilState(depth.Get(), 0); context->RSSetState(raster.Get());
        context->OMSetBlendState(blend.Get(), nullptr, ~0u);
        D3D11_VIEWPORT viewport{0, 0, FLOAT(width), FLOAT(height), 0, 1}; context->RSSetViewports(1, &viewport);
        context->Draw(3, 0);
        check(device->GetDeviceRemovedReason(), "present_draw_status");
        context->OMSetRenderTargets(0, nullptr, nullptr);
        sv = nullptr; context->PSSetShaderResources(0, 1, &sv);
        context->VSSetShader(nullptr, nullptr, 0); context->PSSetShader(nullptr, nullptr, 0);
        context->OMSetBlendState(nullptr, nullptr, ~0u); context->OMSetDepthStencilState(nullptr, 0); context->RSSetState(nullptr);
    }
};
