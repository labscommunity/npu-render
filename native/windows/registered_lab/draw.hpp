// Included inside the registered UMD namespace after allocation operations.
SIZE_T APIENTRY srv_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATESHADERRESOURCEVIEW *) { return sizeof(View); }
void APIENTRY create_srv(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATESHADERRESOURCEVIEW *a,
                        D3D10DDI_HSHADERRESOURCEVIEW out, D3D10DDI_HRTSHADERRESOURCEVIEW) {
    new (out.pDrvPrivate) View();
    guard(h, __FUNCTION__, [&] {
        require(a != nullptr);
        auto &r = resource(a->hDrvResource);
        require(!r.buffer_bytes && r.usage == D3D10_DDI_USAGE_DEFAULT && r.allocation->context == device(h).context &&
                a->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D && UINT(a->Format) == r.allocation->surface.format &&
                !a->Tex2D.MostDetailedMip && a->Tex2D.MipLevels == 1 && !a->Tex2D.FirstArraySlice && a->Tex2D.ArraySize == 1);
        static_cast<View *>(out.pDrvPrivate)->allocation = r.allocation;
    });
}
void APIENTRY destroy_srv(D3D10DDI_HDEVICE h, D3D10DDI_HSHADERRESOURCEVIEW out) {
    guard(h, __FUNCTION__, [&] { static_cast<View *>(out.pDrvPrivate)->~View(); });
}
void APIENTRY set_sources(D3D10DDI_HDEVICE h, UINT start, UINT count, const D3D10DDI_HSHADERRESOURCEVIEW *views) {
    guard(h, __FUNCTION__, [&] {
        require(start < 128 && count <= 128 - start && (!count || views));
        std::shared_ptr<Allocation> source = device(h).bound_source;
        for (UINT i = 0; i < count; ++i) {
            require(start + i == 0 || !views[i].pDrvPrivate);
            if (start + i == 0) {
                source = views[i].pDrvPrivate ? static_cast<View *>(views[i].pDrvPrivate)->allocation : nullptr;
                require(!views[i].pDrvPrivate || (source && source->context == device(h).context));
            }
        }
        device(h).bound_source = std::move(source);
    });
}
SIZE_T APIENTRY shader_size(D3D10DDI_HDEVICE, const UINT *, const D3D10DDIARG_STAGE_IO_SIGNATURES *) { return sizeof(Shader); }
template<UINT Kind>
void APIENTRY create_shader(D3D10DDI_HDEVICE h, const UINT *code, D3D10DDI_HSHADER out,
                           D3D10DDI_HRTSHADER, const D3D10DDIARG_STAGE_IO_SIGNATURES *) {
    new (out.pDrvPrivate) Shader();
    guard(h, __FUNCTION__, [&] {
        const auto &expected = Kind == 1 ? device(h).vertex_code : device(h).pixel_code;
        bool supported = code && expected.size() >= 2 && code[1] == expected.size() && std::equal(expected.begin(), expected.end(), code);
        if constexpr (Kind == 1 || Kind == 2) {
            if (!supported && code && code[1] >= 3 && code[1] <= 16384) {
                try {
                    auto program = std::make_shared<nr::shader::Program>(Kind == 1 ?
                        nr::shader::parse_vertex(code, code[1]) : nr::shader::parse_pixel(code, code[1]));
                    nr::shader::VertexExecutor::validate(*program);
                    auto &shader = *static_cast<Shader *>(out.pDrvPrivate);
                    (Kind == 1 ? shader.vertex_program : shader.pixel_program) = std::move(program);
                    supported = true;
                    lab_trace("{\"event\":\"%s_program_created\",\"words\":%u,\"execution_performed\":false}\n", Kind == 1 ? "vertex" : "pixel", code[1]);
                } catch (const std::invalid_argument &) { /* Preserve the rejected stream below. */ }
            }
        }
        if (!supported) lab_shader_capture(Kind, code);
        require(supported);
        static_cast<Shader *>(out.pDrvPrivate)->kind = Kind;
    });
}
void APIENTRY destroy_shader(D3D10DDI_HDEVICE h, D3D10DDI_HSHADER shader) {
    guard(h, __FUNCTION__, [&] { static_cast<Shader *>(shader.pDrvPrivate)->~Shader(); });
}
template<UINT Kind>
void APIENTRY set_shader(D3D10DDI_HDEVICE h, D3D10DDI_HSHADER shader) {
    guard(h, __FUNCTION__, [&] {
        const UINT value = shader.pDrvPrivate ? static_cast<Shader *>(shader.pDrvPrivate)->kind : 0;
        require(!shader.pDrvPrivate || value == Kind);
        (Kind == 1 ? device(h).vertex_shader : device(h).pixel_shader) = value;
        if constexpr (Kind == 1)
            device(h).vertex_program = shader.pDrvPrivate ? static_cast<Shader *>(shader.pDrvPrivate)->vertex_program : nullptr;
        else
            device(h).pixel_program = shader.pDrvPrivate ? static_cast<Shader *>(shader.pDrvPrivate)->pixel_program : nullptr;
    });
}
void APIENTRY set_viewports(D3D10DDI_HDEVICE h, UINT count, UINT, const D3D10_DDI_VIEWPORT *values) {
    guard(h, __FUNCTION__, [&] {
        require(count <= 16 && (!count || values));
        device(h).viewport_count = count;
        device(h).viewport = count == 1 ? values[0] : D3D10_DDI_VIEWPORT{};
    });
}
void APIENTRY set_topology(D3D10DDI_HDEVICE h, D3D10_DDI_PRIMITIVE_TOPOLOGY value) {
    guard(h, __FUNCTION__, [&] { device(h).topology = value; });
}
std::vector<uint8_t> read_pixels(Allocation &source) {
    require(!source.locked && !source.context->poisoned);
    const auto &s = source.surface;
    std::vector<uint8_t> result(size_t(s.width) * s.height * 4);
    std::unique_ptr<Allocation> staging;
    Allocation *readable = &source;
    if (source.runtime_resource) {
        staging = std::make_unique<Allocation>(source.context);
        staging->create(s.width, s.height, s.format);
        source.context->copy(source.handle, staging->handle);
        counters.copies += s.allocation_bytes;
        readable = staging.get();
    }
    Mapping read(*readable, true);
    for (UINT y = 0; y < s.height; ++y)
        std::memcpy(result.data() + size_t(y) * s.width * 4,
                    static_cast<const uint8_t *>(read.data) + size_t(y) * s.row_pitch, s.width * 4);
    read.finish();
    return result;
}
void APIENTRY draw(D3D10DDI_HDEVICE h, UINT count, UINT first) {
    guard(h, __FUNCTION__, [&] {
        auto &d = device(h);
        const auto target = d.bound_target, source = d.bound_source;
        require(count == 3 && !first && d.topology == D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST &&
                d.vertex_shader == 1 && !d.vertex_program && d.pixel_shader == 2 && !d.pixel_program && target && source && target != source &&
                target->handle != source->handle && target->context == d.context && source->context == d.context &&
                !target->locked && !source->locked && !d.context->poisoned);
        const auto &s = target->surface;
        require(s.width == source->surface.width && s.height == source->surface.height && s.format == source->surface.format);
        require(d.viewport_count == 1 && d.viewport.TopLeftX == 0 && d.viewport.TopLeftY == 0 &&
                d.viewport.Width == FLOAT(s.width) && d.viewport.Height == FLOAT(s.height) &&
                d.viewport.MinDepth == 0 && d.viewport.MaxDepth == 1);
        require(!d.depth.DepthEnable && !d.depth.StencilEnable && d.raster.FillMode == D3D10_DDI_FILL_SOLID &&
                d.raster.CullMode == D3D10_DDI_CULL_NONE && !d.raster.ScissorEnable && !d.raster.MultisampleEnable &&
                !d.raster.AntialiasedLineEnable && !d.raster.DepthBias && d.raster.DepthBiasClamp == 0 &&
                d.raster.SlopeScaledDepthBias == 0 && !d.blend.AlphaToCoverageEnable &&
                d.blend.RenderTargetWriteMask[0] == 15 && d.sample_mask == ~0u);
        if (d.blend.BlendEnable[0])
            require(d.blend.SrcBlend == D3D10_DDI_BLEND_ONE && d.blend.DestBlend == D3D10_DDI_BLEND_INV_SRC_ALPHA &&
                    d.blend.BlendOp == D3D10_DDI_BLEND_OP_ADD && d.blend.SrcBlendAlpha == D3D10_DDI_BLEND_ONE &&
                    d.blend.DestBlendAlpha == D3D10_DDI_BLEND_INV_SRC_ALPHA && d.blend.BlendOpAlpha == D3D10_DDI_BLEND_OP_ADD);
        // All state validation precedes preparation and any resource traffic.
        if (!d.executor) d.executor = std::make_unique<BlendExecutor>(d.backend);
        auto foreground = read_pixels(*source);
        auto background = d.blend.BlendEnable[0] ? read_pixels(*target) : std::vector<uint8_t>(foreground.size());
        auto result = d.executor->compose(s.width, s.height, background, foreground);
        ++counters.executions;
        if (d.backend == "NPU_LAB_FILE") ++counters.remote_npu_executions;
        else if (d.backend == "NPU") ++counters.npu_executions;
        else if (d.backend == "GPU") ++counters.gpu_executions;
        else ++counters.cpu_executions;
        strncpy_s(counters.last_execution_device, result.execution_device.c_str(), _TRUNCATE);
        counters.inference_input_bytes += foreground.size() + background.size();
        counters.inference_output_bytes += result.pixels.size();
        // Commit only successful inference. This is an explicit CPU upload into
        // VidMm, counted even when inference itself executes on the NPU.
        upload(*target, nullptr, result.pixels.data(), s.width * 4);
        ++counters.draws;
    });
}
void APIENTRY null_shader(D3D10DDI_HDEVICE h, D3D10DDI_HSHADER value) { guard(h, __FUNCTION__, [&] { require(!value.pDrvPrivate); }); }
void APIENTRY null_views(D3D10DDI_HDEVICE h, UINT start, UINT count, const D3D10DDI_HSHADERRESOURCEVIEW *values) {
    guard(h, __FUNCTION__, [&] { require(start <= 128 && count <= 128 - start && (!count || values));
        for (UINT i = 0; i < count; ++i) require(!values[i].pDrvPrivate); });
}
void APIENTRY null_stream(D3D10DDI_HDEVICE h, UINT count, UINT, const D3D10DDI_HRESOURCE *values, const UINT *) {
    guard(h, __FUNCTION__, [&] { require(count <= 4 && (!count || values)); for (UINT i = 0; i < count; ++i) require(!values[i].pDrvPrivate); });
}
void APIENTRY null_predication(D3D10DDI_HDEVICE h, D3D10DDI_HQUERY value, BOOL) { guard(h, __FUNCTION__, [&] { require(!value.pDrvPrivate); }); }
void APIENTRY set_scissors(D3D10DDI_HDEVICE h, UINT count, UINT, const D3D10_DDI_RECT *values) {
    guard(h, __FUNCTION__, [&] { require(count <= 16 && (!count || values)); }); // Enabled scissoring is rejected before drawing.
}
void APIENTRY view_hazard(D3D10DDI_HDEVICE, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRESOURCE) {} // Blocking mappings/submissions order access.
void bind_draw_functions(D3D10DDI_DEVICEFUNCS &f) {
    f.pfnCalcPrivateShaderResourceViewSize = srv_size; f.pfnCreateShaderResourceView = create_srv;
    f.pfnDestroyShaderResourceView = destroy_srv; f.pfnPsSetShaderResources = set_sources;
    f.pfnCalcPrivateShaderSize = shader_size; f.pfnCreateVertexShader = create_shader<1>; f.pfnCreatePixelShader = create_shader<2>;
    f.pfnDestroyShader = destroy_shader; f.pfnVsSetShader = set_shader<1>; f.pfnPsSetShader = set_shader<2>;
    f.pfnSetViewports = set_viewports; f.pfnIaSetTopology = set_topology; f.pfnDraw = draw;
    f.pfnGsSetShader = null_shader; f.pfnIaSetInputLayout = set_layout;
    f.pfnCalcPrivateElementLayoutSize = layout_size; f.pfnCreateElementLayout = create_layout;
    f.pfnDestroyElementLayout = destroy_layout;
    f.pfnVsSetConstantBuffers = set_constant_buffers<0>;
    f.pfnGsSetConstantBuffers = set_constant_buffers<1>;
    f.pfnPsSetConstantBuffers = set_constant_buffers<2>;
    f.pfnVsSetShaderResources = f.pfnGsSetShaderResources = null_views;
    f.pfnIaSetVertexBuffers = set_vertex_buffers; f.pfnIaSetIndexBuffer = set_index_buffer;
    f.pfnSoSetTargets = null_stream; f.pfnSetPredication = null_predication;
    f.pfnSetScissorRects = set_scissors; f.pfnShaderResourceViewReadAfterWriteHazard = view_hazard;
}
