// Real composition provider with explicit, test-only runtime allocation shims.
int test_draws(const D3DDDI_DEVICECALLBACKS &kt, const D3D10DDI_CORELAYER_DEVICECALLBACKS &um, const char *backend, bool desktop_size) {
    TEST(nr_fill_configure("AUTO") == FALSE && nr_fill_configure(backend));
    emulate_copies = true; last_error = S_OK;
    alignas(Device) unsigned char storage[sizeof(Device)]{};
    D3D10DDI_DEVICEFUNCS f{}; DXGI_DDI_BASE_FUNCTIONS dxgi{};
    D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate = storage; create.hRTDevice.handle = runtime_handle;
    create.pDeviceFuncs = &f; create.pUMCallbacks = &um; create.pKTCallbacks = &kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions = &dxgi; create.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({}, &create) == DXGI_STATUS_NO_REDIRECTION);
    TEST(!nr_fill_configure(backend)); // Configuration cannot change beneath a live device.
    const auto h = create.hDrvDevice;
    auto &d = device(h);
    Shader vs{}, ps{}, invalid{};
    D3D10DDI_HSHADER hv{}, hp{}, hi{}; hv.pDrvPrivate = &vs; hp.pDrvPrivate = &ps; hi.pDrvPrivate = &invalid;
    f.pfnCreateVertexShader(h, d.vertex_code.data(), hv, {}, nullptr);
    f.pfnCreatePixelShader(h, d.pixel_code.data(), hp, {}, nullptr);
    auto altered = d.pixel_code; altered.back() ^= 1;
    f.pfnCreatePixelShader(h, altered.data(), hi, {}, nullptr);
    TEST(last_error == E_INVALIDARG && !invalid.kind && !counters.executions);
    last_error = S_OK;
    f.pfnVsSetShader(h, hv); f.pfnPsSetShader(h, hp);
    f.pfnIaSetTopology(h, D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D10_DDI_DEPTH_STENCIL_DESC depth{}; D3D10DDI_HDEPTHSTENCILSTATE hd{}; hd.pDrvPrivate = &depth;
    f.pfnSetDepthStencilState(h, hd, 0);
    D3D10_DDI_RASTERIZER_DESC raster{}; raster.FillMode = D3D10_DDI_FILL_SOLID; raster.CullMode = D3D10_DDI_CULL_NONE;
    D3D10DDI_HRASTERIZERSTATE hr{}; hr.pDrvPrivate = &raster; f.pfnSetRasterizerState(h, hr);
    D3D10_DDI_BLEND_DESC blend{};
    blend.BlendEnable[0] = TRUE; blend.RenderTargetWriteMask[0] = 15;
    blend.SrcBlend = blend.SrcBlendAlpha = D3D10_DDI_BLEND_ONE;
    blend.DestBlend = blend.DestBlendAlpha = D3D10_DDI_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = blend.BlendOpAlpha = D3D10_DDI_BLEND_OP_ADD;
    D3D10DDI_HBLENDSTATE hb{}; hb.pDrvPrivate = &blend;
    UINT checks = 0;
    const std::array<std::pair<UINT, UINT>, 2> extents = desktop_size ?
        std::array<std::pair<UINT, UINT>, 2>{{{1920, 1080}, {2880, 1800}}} :
        std::array<std::pair<UINT, UINT>, 2>{{{64, 16}, {132, 16}}};
    for (auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM})
        for (bool shared : {false, true}) for (const auto &[width, height] : extents) {
            std::vector<unsigned char> foreground(size_t(width) * height * 4), background(foreground.size());
            for (size_t i = 0; i < foreground.size(); i += 4) {
                const auto alpha = static_cast<unsigned char>((i / 4 * 37) % 256);
                for (size_t c = 0; c < 3; ++c) {
                    foreground[i + c] = static_cast<unsigned char>((i / 4 + c * 23) % (UINT(alpha) + 1));
                    background[i + c] = static_cast<unsigned char>((i / 4 + c * 53) % 256);
                }
                foreground[i + 3] = alpha; background[i + 3] = 255;
            }
            D3D10DDI_MIPINFO mip{}; mip.TexelWidth = width; mip.TexelHeight = height; mip.TexelDepth = 1;
            D3D10_DDIARG_SUBRESOURCE_UP initial{}; initial.pSysMem = background.data(); initial.SysMemPitch = width * 4;
            D3D10DDIARG_CREATERESOURCE args{};
            args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D; args.MipLevels = args.ArraySize = 1;
            args.pMipInfoList = &mip; args.Format = format; args.SampleDesc.Count = 1;
            args.Usage = D3D10_DDI_USAGE_DEFAULT; args.BindFlags = D3D10_DDI_BIND_RENDER_TARGET;
            args.MiscFlags = shared ? D3D10_DDI_RESOURCE_MISC_SHARED : 0; args.pInitialDataUP = &initial;
            alignas(Resource) unsigned char target_storage[sizeof(Resource)]{}, source_storage[sizeof(Resource)]{};
            D3D10DDI_HRESOURCE target{}, source{}; target.pDrvPrivate = target_storage; source.pDrvPrivate = source_storage;
            D3D10DDI_HRTRESOURCE rt{}; rt.handle = shared ? reinterpret_cast<HANDLE>(uintptr_t(0xa001)) : nullptr;
            f.pfnCreateResource(h, &args, target, rt);
            args.BindFlags = D3D10_DDI_BIND_SHADER_RESOURCE; initial.pSysMem = foreground.data();
            rt.handle = shared ? reinterpret_cast<HANDLE>(uintptr_t(0xa002)) : nullptr;
            f.pfnCreateResource(h, &args, source, rt);
            TEST(last_error == S_OK);
            const auto target_index = resource(target).allocation->handle;
            const auto pitch = resource(target).allocation->surface.row_pitch;
            View tv{}, sv{}; D3D10DDI_HRENDERTARGETVIEW ht{}; ht.pDrvPrivate = &tv;
            D3D10DDI_HSHADERRESOURCEVIEW hs{}; hs.pDrvPrivate = &sv;
            D3D10DDIARG_CREATERENDERTARGETVIEW target_view{};
            target_view.hDrvResource = target; target_view.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
            target_view.Format = format; target_view.Tex2D.ArraySize = 1;
            f.pfnCreateRenderTargetView(h, &target_view, ht, {});
            D3D10DDIARG_CREATESHADERRESOURCEVIEW source_view{};
            source_view.hDrvResource = source; source_view.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
            source_view.Format = format; source_view.Tex2D.ArraySize = source_view.Tex2D.MipLevels = 1;
            f.pfnCreateShaderResourceView(h, &source_view, hs, {});
            f.pfnSetRenderTargets(h, &ht, 1, 0, {}); f.pfnPsSetShaderResources(h, 0, 1, &hs);
            D3D10_DDI_VIEWPORT viewport{}; viewport.Width = FLOAT(width); viewport.Height = FLOAT(height); viewport.MaxDepth = 1;
            f.pfnSetViewports(h, 1, 0, &viewport);
            TEST(last_error == S_OK);
            const auto executions = counters.executions, allocations = counters.allocations;
            const auto before = memory[target_index];
            if (!checks) {
                // Runtime shader wrapper destruction must not invalidate a
                // bound program. Exercise its real stage executor separately;
                // generic rasterization is still rejected by the Draw DDI.
                const auto code = shader_tokens("float4 main(float4 p:POSITION):SV_Position{return p*float4(2,3,4,1);}", "vs_4_0");
                alignas(Shader) unsigned char generic_storage[sizeof(Shader)]{};
                D3D10DDI_HSHADER generic{}; generic.pDrvPrivate = generic_storage;
                f.pfnCreateVertexShader(h, code.data(), generic, {}, nullptr);
                TEST(last_error == S_OK);
                f.pfnVsSetShader(h, generic);
                TEST(d.vertex_program);
                const std::weak_ptr<const nr::shader::Program> retained = d.vertex_program;
                f.pfnDestroyShader(h, generic);
                TEST(last_error == S_OK && !retained.expired());
                std::vector<nr::shader::VertexRegisters> vertices(16);
                const std::array<float, 4> input{0.25f, -0.5f, 0.125f, 1.0f};
                for (auto &v : vertices) std::memcpy(v[0].data(), input.data(), sizeof(input));
                nr::shader::VertexExecutor executor(*d.vertex_program, UINT(vertices.size()), backend);
                const auto output = executor.execute(vertices, {});
                const std::array<float, 4> expected{0.5f, -1.5f, 0.5f, 1.0f};
                for (const auto &v : output.vertices)
                    TEST(!std::memcmp(v[d.vertex_program->position_output].data(), expected.data(), sizeof(expected)));
                f.pfnSetBlendState(h, hb, nullptr, ~0u);
                f.pfnDraw(h, 3, 0);
                TEST(last_error == E_INVALIDARG && counters.executions == executions &&
                     counters.allocations == allocations && memory[target_index] == before);
                last_error = S_OK; f.pfnVsSetShader(h, hv);
                TEST(last_error == S_OK && retained.expired());
                std::printf("{\"bound_vertex_stage\":true,\"vertices\":16,\"execution_device\":\"%s\",\"exact_bits_verified\":true,\"desktop_offload_tested\":false}\n", output.execution_device.c_str());
            }
            depth.DepthEnable = TRUE; f.pfnSetDepthStencilState(h, hd, 0);
            f.pfnDraw(h, 3, 0);
            TEST(last_error == E_INVALIDARG && counters.executions == executions && counters.allocations == allocations && memory[target_index] == before);
            depth.DepthEnable = FALSE; f.pfnSetDepthStencilState(h, hd, 0); last_error = S_OK;
            // UMD bindings retain backing storage after the runtime-owned
            // resource/view wrappers go away. The host shim tests that ownership.
            f.pfnDestroyRenderTargetView(h, ht); f.pfnDestroyShaderResourceView(h, hs);
            new (&tv) View(); new (&sv) View(); // Restore local C++ objects after explicit DDI destruction.
            f.pfnDestroyResource(h, target); f.pfnDestroyResource(h, source);
            for (UINT pass = 0; pass < 2; ++pass) {
                blend.BlendEnable[0] = pass == 0; f.pfnSetBlendState(h, hb, nullptr, ~0u);
                f.pfnDraw(h, 3, 0);
                TEST(last_error == S_OK && counters.executions == executions + pass + 1);
                UINT max_error = 0;
                for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) for (UINT c = 0; c < 4; ++c) {
                    const auto i = (size_t(y) * width + x) * 4;
                    const auto expected = pass ? foreground[i + c] : std::clamp<int>(int(std::lround(
                        foreground[i + c] + background[i + c] * (255 - foreground[i + 3]) / 255.0)), 0, 255);
                    const auto actual = memory[target_index][size_t(y) * pitch + x * 4 + c];
                    max_error = std::max(max_error, UINT(std::abs(int(actual) - expected)));
                }
                TEST(max_error <= (pass ? 0u : 1u)); ++checks;
                std::printf("{\"component_draw\":true,\"format\":%u,\"width\":%u,\"height\":%u,\"shared\":%s,\"pass\":%u,\"max_error\":%u,\"verified_bytes\":%zu}\n",
                    UINT(format), width, height, shared ? "true" : "false", pass, max_error, foreground.size());
            }
            f.pfnSetRenderTargets(h, nullptr, 0, 8, {});
            const D3D10DDI_HSHADERRESOURCEVIEW none{}; f.pfnPsSetShaderResources(h, 0, 1, &none);
            TEST(!counters.live_allocations && resource_handles.empty() && pending_copies.empty());
        }
    f.pfnDestroyDevice(h);
    TEST(!counters.live_contexts && !counters.cleanup_errors && !contract_error && checks == 16 && counters.draws == 16);
    TEST(counters.npu_executions == (std::strcmp(backend, "NPU") == 0 ? 16u : 0u));
    TEST(counters.cpu_executions == (std::strcmp(backend, "CPU") == 0 ? 16u : 0u));
    TEST(counters.gpu_executions == (std::strcmp(backend, "GPU") == 0 ? 16u : 0u));
    std::printf("{\"kind\":\"registered-ddi-runtime-shim\",\"requested_backend\":\"%s\",\"execution_device\":\"%s\",\"verified_draws\":16,\"registered_adapter_tested\":false,\"desktop_offload_tested\":false}\n",
                backend, counters.last_execution_device);
    return 0;
}
