// Included inside the registered UMD namespace after allocation operations.
SIZE_T APIENTRY srv_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATESHADERRESOURCEVIEW *) { return sizeof(View); }
void APIENTRY create_srv(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATESHADERRESOURCEVIEW *a,
                        D3D10DDI_HSHADERRESOURCEVIEW out, D3D10DDI_HRTSHADERRESOURCEVIEW) {
    new (out.pDrvPrivate) View();
    guard(h, __FUNCTION__, [&] {
        require(a != nullptr);
        lab_trace("{\"event\":\"shader_resource_view\",\"dimension\":%u,\"format\":%u,\"first\":%u,\"count\":%u}\n",
            UINT(a->ResourceDimension), UINT(a->Format), a->Buffer.FirstElement, a->Buffer.NumElements);
        auto &r = resource(a->hDrvResource);
        require(r.allocation->context == device(h).context);
        if (r.buffer_bytes) {
            require((r.bind_flags & D3D10_DDI_BIND_SHADER_RESOURCE) &&
                a->ResourceDimension == D3D10DDIRESOURCE_BUFFER && a->Format == DXGI_FORMAT_R8_UINT &&
                a->Buffer.NumElements && uint64_t(a->Buffer.FirstElement) + a->Buffer.NumElements <= r.buffer_bytes);
            auto &view = *static_cast<View *>(out.pDrvPrivate);
            view.allocation = r.allocation; view.format = UINT(a->Format); view.buffer = true;
            view.first_element = a->Buffer.FirstElement; view.elements = a->Buffer.NumElements;
            return;
        }
        require(!r.buffer_bytes && (r.usage == D3D10_DDI_USAGE_DEFAULT || r.usage == D3D10_DDI_USAGE_IMMUTABLE) && r.allocation->context == device(h).context &&
                a->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D && UINT(a->Format) == r.allocation->surface.format &&
                a->Tex2D.MostDetailedMip < mip_count(r) && a->Tex2D.MipLevels == 1 && !a->Tex2D.FirstArraySlice && a->Tex2D.ArraySize == 1);
        static_cast<View *>(out.pDrvPrivate)->allocation = mip_allocation(r, a->Tex2D.MostDetailedMip);
    });
}
void APIENTRY destroy_srv(D3D10DDI_HDEVICE h, D3D10DDI_HSHADERRESOURCEVIEW out) {
    guard(h, __FUNCTION__, [&] { static_cast<View *>(out.pDrvPrivate)->~View(); });
}
template<UINT Stage>
void APIENTRY set_sources(D3D10DDI_HDEVICE h, UINT start, UINT count, const D3D10DDI_HSHADERRESOURCEVIEW *views) {
    guard(h, __FUNCTION__, [&] {
        require(start <= 128 && count <= 128 - start && (!count || views));
        auto &d = device(h); auto next = d.shader_sources[Stage];
        for (UINT i = 0; i < count; ++i) {
            next[start+i] = views[i].pDrvPrivate ? *static_cast<View *>(views[i].pDrvPrivate) : View{};
            const auto &source = next[start+i].allocation;
            require(!views[i].pDrvPrivate || (source && source->context == d.context));
        }
        d.shader_sources[Stage] = std::move(next);
        // Keep the legacy slot-zero SAMPLE alias separate from the complete
        // view table used by typed resource loads.
        if constexpr (Stage == 2) d.bound_source = d.shader_sources[2][0].buffer ? nullptr : d.shader_sources[2][0].allocation;
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
    const UINT row_bytes = s.width * texel_bytes(s.format);
    std::vector<uint8_t> result(size_t(row_bytes) * s.height);
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
        std::memcpy(result.data() + size_t(y) * row_bytes,
                    static_cast<const uint8_t *>(read.data) + size_t(y) * s.row_pitch, row_bytes);
    read.finish();
    return result;
}
#include "draw_capture.hpp"
#include "indexed_draw.hpp"
void APIENTRY draw_indexed(D3D10DDI_HDEVICE h, UINT count, UINT first, INT base) {
    guard(h, __FUNCTION__, [&] {
        try { execute_indexed_draw(device(h), count, first, base); }
        catch (...) { lab_capture_indexed_draw(device(h), count, first, base); throw; }
    });
}
void APIENTRY draw_instanced(D3D10DDI_HDEVICE h,UINT count,UINT instances,UINT first,UINT first_instance){
    guard(h,__FUNCTION__,[&]{auto &d=device(h);if(!count || !instances)return;
        try{execute_triangle_draw(d,fetch_draw_vertices(d,count,first,instances,first_instance),false);}
        catch(...){lab_capture_indexed_draw(d,count,first,0,false,instances,first_instance);throw;}
    });
}
void APIENTRY draw_indexed_instanced(D3D10DDI_HDEVICE h,UINT count,UINT instances,UINT first,INT base,UINT first_instance){
    guard(h,__FUNCTION__,[&]{auto &d=device(h);if(!count || !instances)return;
        try{require(d.topology==D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            execute_triangle_draw(d,fetch_indexed_vertices(d,count,first,base,instances,first_instance),true);}
        catch(...){lab_capture_indexed_draw(d,count,first,base,true,instances,first_instance);throw;}
    });
}
void APIENTRY draw(D3D10DDI_HDEVICE h, UINT count, UINT first) {
    guard(h, __FUNCTION__, [&] {
        auto &d = device(h);
        if (!count) return;
        if (d.vertex_program || d.pixel_program) {
            require(d.vertex_program && d.pixel_program);
            try { execute_triangle_draw(d, fetch_draw_vertices(d, count, first), false); }
            catch (...) { lab_capture_indexed_draw(d, count, first, 0, false); throw; }
            return;
        }
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
        // targets() rejects every non-null depth/stencil view. Without that
        // buffer, Direct3D depth and stencil tests always pass, even when enabled.
        require(d.raster.FillMode == D3D10_DDI_FILL_SOLID &&
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
void APIENTRY null_stream(D3D10DDI_HDEVICE h, UINT count, UINT, const D3D10DDI_HRESOURCE *values, const UINT *) {
    guard(h, __FUNCTION__, [&] { require(count <= 4 && (!count || values)); for (UINT i = 0; i < count; ++i) require(!values[i].pDrvPrivate); });
}
void APIENTRY null_predication(D3D10DDI_HDEVICE h, D3D10DDI_HQUERY value, BOOL) { guard(h, __FUNCTION__, [&] { require(!value.pDrvPrivate); }); }
void APIENTRY set_scissors(D3D10DDI_HDEVICE h, UINT count, UINT, const D3D10_DDI_RECT *values) {
    guard(h, __FUNCTION__, [&] {
        require(count <= 16 && (!count || values));
        auto &scissors = device(h).scissors;
        if (count) scissors.assign(values, values + count); else scissors.clear();
    });
}
void APIENTRY view_hazard(D3D10DDI_HDEVICE, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRESOURCE) {} // Blocking mappings/submissions order access.
void bind_draw_functions(D3D10DDI_DEVICEFUNCS &f) {
    f.pfnCalcPrivateShaderResourceViewSize = srv_size; f.pfnCreateShaderResourceView = create_srv;
    f.pfnDestroyShaderResourceView = destroy_srv; f.pfnPsSetShaderResources = set_sources<2>;
    f.pfnCalcPrivateShaderSize = shader_size; f.pfnCreateVertexShader = create_shader<1>; f.pfnCreatePixelShader = create_shader<2>;
    f.pfnDestroyShader = destroy_shader; f.pfnVsSetShader = set_shader<1>; f.pfnPsSetShader = set_shader<2>;
    f.pfnSetViewports = set_viewports; f.pfnIaSetTopology = set_topology; f.pfnDraw = draw;
    f.pfnDrawIndexed = draw_indexed;
    f.pfnDrawInstanced=draw_instanced;f.pfnDrawIndexedInstanced=draw_indexed_instanced;
    f.pfnGsSetShader = null_shader; f.pfnIaSetInputLayout = set_layout;
    f.pfnCalcPrivateElementLayoutSize = layout_size; f.pfnCreateElementLayout = create_layout;
    f.pfnDestroyElementLayout = destroy_layout;
    f.pfnVsSetConstantBuffers = set_constant_buffers<0>;
    f.pfnGsSetConstantBuffers = set_constant_buffers<1>;
    f.pfnPsSetConstantBuffers = set_constant_buffers<2>;
    f.pfnVsSetShaderResources = set_sources<0>; f.pfnGsSetShaderResources = set_sources<1>;
    f.pfnIaSetVertexBuffers = set_vertex_buffers; f.pfnIaSetIndexBuffer = set_index_buffer;
    f.pfnSoSetTargets = null_stream; f.pfnSetPredication = null_predication;
    f.pfnSetScissorRects = set_scissors; f.pfnShaderResourceViewReadAfterWriteHazard = view_hazard;
}
