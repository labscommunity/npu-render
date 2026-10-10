// Decode IA inputs separately from shader arithmetic. Vertex IDs follow the
// D3D10/11 system-value contract: draw-local ordinal or raw index, without the
// vertex-buffer start/base address offset (functional spec sections 8.4/8.6).
std::vector<nr::shader::VertexRegisters> fetch_vertices_at(Device &d, const std::vector<UINT> &indices,
                                                        const std::vector<UINT> &vertex_ids) {
    require(d.context && !d.context->poisoned && !indices.empty() && indices.size() <= 65536 && indices.size() == vertex_ids.size());
    const UINT builtin = d.vertex_program ? d.vertex_program->vertex_id_input : 16;
    std::vector<nr::shader::VertexRegisters> result(indices.size());
    uint32_t bound = 0;
    if (d.input_elements) for (const auto &e : *d.input_elements) {
        require(e.InputRegister < 16 && e.InputRegister != builtin && e.InputSlot < d.vertex_buffers.size());
        bound |= 1u << e.InputRegister;
        const auto format = element_format(e.Format); const auto &b = d.vertex_buffers[e.InputSlot];
        require(b.allocation && b.allocation->context == d.context && !b.allocation->locked);
        for (const auto index : indices) require(uint64_t(b.offset) + e.AlignedByteOffset + uint64_t(index) * b.stride + format.bytes <= b.bytes);
        Mapping read(*b.allocation, true);
        for (size_t i = 0; i < indices.size(); ++i) {
            auto &v = result[i][e.InputRegister]; v = {0, 0, 0, format.integer ? 1u : 0x3f800000u};
            const auto *data = static_cast<const uint8_t *>(read.data) + b.offset + e.AlignedByteOffset + uint64_t(indices[i]) * b.stride;
            if (format.normalized) for (UINT c = 0; c < 4; ++c) { const float f = float(data[c]) / 255; std::memcpy(&v[c], &f, 4); }
            else if (format.half) for (UINT c = 0; c < format.lanes; ++c) { uint16_t f; std::memcpy(&f, data + c * 2, 2); v[c] = half_to_float_bits(f); }
            else std::memcpy(v.data(), data, format.bytes);
        }
        read.finish();
    }
    if (d.vertex_program) for (UINT reg = 0; reg < 16; ++reg)
        require(!d.vertex_program->inputs[reg] || reg == builtin || (bound & (1u << reg)));
    if (builtin < 16) for (size_t i = 0; i < result.size(); ++i) result[i][builtin][0] = vertex_ids[i];
    return result;
}
std::vector<nr::shader::VertexRegisters> fetch_indexed_vertices(Device &d, UINT count, UINT first, INT base) {
    const auto &ib = d.index_buffer;
    require(count && count <= 65536 && !(count % 3) && ib.allocation &&
        ib.allocation->context == d.context && !ib.allocation->locked && !d.context->poisoned &&
        (ib.stride == 2 || ib.stride == 4) && uint64_t(ib.offset) + (uint64_t(first) + count) * ib.stride <= ib.bytes);
    std::vector<UINT> indices(count), ids(count);
    {
        Mapping read(*ib.allocation, true);
        for (UINT i = 0; i < count; ++i) {
            UINT index = 0; std::memcpy(&index, static_cast<const uint8_t *>(read.data) + ib.offset + (uint64_t(first) + i) * ib.stride, ib.stride);
            const int64_t adjusted = int64_t(index) + base;
            require(adjusted >= 0 && adjusted <= UINT_MAX); indices[i] = UINT(adjusted); ids[i] = index;
        }
        read.finish();
    }
    return fetch_vertices_at(d, indices, ids);
}
std::vector<nr::shader::VertexRegisters> fetch_draw_vertices(Device &d, UINT count, UINT first) {
    require(count >= 3 && count <= 65536 && uint64_t(first) + count - 1 <= UINT_MAX);
    std::vector<UINT> indices, ids;
    auto append = [&](UINT id) { indices.push_back(first + id); ids.push_back(id); };
    if (d.topology == D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
        require(!(count % 3));
        for (UINT i = 0; i < count; ++i) append(i);
    } else {
        require(d.topology == D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP && uint64_t(count - 2) * 3 <= 65536);
        for (UINT i = 0; i < count - 2; ++i) {
            append(i + (i & 1)); append(i + ((i & 1) ? 0 : 1)); append(i + 2);
        }
    }
    return fetch_vertices_at(d, indices, ids);
}
nr::shader::VertexConstants fetch_constants(Device &d, UINT stage, const nr::shader::Program &program) {
    nr::shader::VertexConstants values;
    for (UINT i = 0; i < values.size(); ++i) if (program.constants[i]) {
        const auto &a = d.constant_buffers[stage][i]; const auto bytes = uint64_t(program.constants[i]) * 16;
        require(a && a->context == d.context && !a->locked && bytes <= d.constant_bytes[stage][i]);
        values[i].resize(program.constants[i]); Mapping read(*a, true);
        std::memcpy(values[i].data(), read.data, size_t(bytes)); read.finish();
    }
    return values;
}
void execute_triangle_draw(Device &d, const std::vector<nr::shader::VertexRegisters> &inputs, bool indexed) {
    const auto count = UINT(inputs.size());
    require(count && count <= 65536 && !(count % 3));
    const auto started = GetTickCount64();
    if (!d.vertex_program || !d.pixel_program) throw Error{DXGI_ERROR_UNSUPPORTED};
    const auto target = d.bound_target;
    require(target && target->context == d.context && !target->locked && !d.context->poisoned &&
        d.vertex_shader == 1 && d.pixel_shader == 2);
    const auto &s = target->surface;
    require((s.format == 28 || s.format == 87) && d.viewport_count == 1 && d.viewport.TopLeftX == 0 && d.viewport.TopLeftY == 0 &&
        d.viewport.Width == FLOAT(s.width) && d.viewport.Height == FLOAT(s.height) &&
        !d.depth.DepthEnable && !d.depth.StencilEnable && d.raster.FillMode == D3D10_DDI_FILL_SOLID &&
        d.raster.CullMode == D3D10_DDI_CULL_NONE && !d.raster.MultisampleEnable && !d.raster.AntialiasedLineEnable &&
        !d.raster.DepthBias && d.raster.DepthBiasClamp == 0 && d.raster.SlopeScaledDepthBias == 0 &&
        !d.blend.AlphaToCoverageEnable && d.sample_mask == ~0u && d.blend.RenderTargetWriteMask[0] && d.blend.RenderTargetWriteMask[0] <= 15);
    const std::array<int32_t,4> full{0,0,int32_t(s.width),int32_t(s.height)};
    auto clip = full;
    if (d.raster.ScissorEnable) {
        require(d.scissors.size() == 1); const auto &r = d.scissors[0]; clip = {r.left,r.top,r.right,r.bottom};
        require(clip[0] >= 0 && clip[1] >= 0 && clip[0] <= clip[2] && clip[1] <= clip[3] && clip[2] <= full[2] && clip[3] <= full[3]);
    }
    const bool blend = d.blend.BlendEnable[0] != 0;
    if (blend) require(d.blend.SrcBlend == D3D10_DDI_BLEND_ONE && d.blend.DestBlend == D3D10_DDI_BLEND_INV_SRC_ALPHA &&
        d.blend.BlendOp == D3D10_DDI_BLEND_OP_ADD && d.blend.SrcBlendAlpha == D3D10_DDI_BLEND_ONE &&
        d.blend.DestBlendAlpha == D3D10_DDI_BLEND_INV_SRC_ALPHA && d.blend.BlendOpAlpha == D3D10_DDI_BLEND_OP_ADD);
    const auto vc = fetch_constants(d, 0, *d.vertex_program), pc = fetch_constants(d, 2, *d.pixel_program);
    const bool textured = std::any_of(d.pixel_program->textures.begin(),d.pixel_program->textures.end(),[](bool v){return v;});
    nr::raster::PackedTexture texture; bool linear=false;
    if (textured) {
        const auto source = d.bound_source; const auto &sampler = d.samplers[2][0];
        linear=sampler.desc.Filter==D3D10_DDI_FILTER_MIN_MAG_MIP_LINEAR;
        if(linear) nr::raster::LinearTextureDrawExecutor::validate_shader(*d.pixel_program);
        else { require(clip==full && !blend); nr::raster::PointTextureDrawExecutor::validate_shader(*d.pixel_program); }
        require(count == 6 && d.blend.RenderTargetWriteMask[0] == 15 && source && source != target && source->handle != target->handle &&
            source->context == d.context && !source->locked && sampler.bound &&
            (linear || sampler.desc.Filter == D3D10_DDI_FILTER_MIN_MAG_MIP_POINT) &&
            sampler.desc.AddressU == D3D10_DDI_TEXTURE_ADDRESS_CLAMP && sampler.desc.AddressV == D3D10_DDI_TEXTURE_ADDRESS_CLAMP &&
            sampler.desc.AddressW == D3D10_DDI_TEXTURE_ADDRESS_CLAMP && sampler.desc.MipLODBias == 0 && sampler.desc.MinLOD == 0 && sampler.desc.MaxLOD >= 0);
        const auto &ss = source->surface;
        require((ss.format == 28 || ss.format == 87) && ss.width <= 8192 && ss.height <= 8192 && uint64_t(ss.width)*ss.height <= 4*1024*1024);
        const auto source_bytes = read_pixels(*source); texture = {ss.width,ss.height,ss.format,{}}; texture.pixels.resize(source_bytes.size()/4);
        std::memcpy(texture.pixels.data(),source_bytes.data(),source_bytes.size());
    }
    const auto bytes = read_pixels(*target); std::vector<nr::raster::PackedColor> background(bytes.size() / 4);
    std::memcpy(background.data(), bytes.data(), bytes.size());
    const nr::raster::Viewport viewport{d.viewport.TopLeftX, d.viewport.TopLeftY, d.viewport.Width, d.viewport.Height, d.viewport.MinDepth, d.viewport.MaxDepth};
    nr::raster::UniformDrawExecutor::Result result;
    if (d.backend == "NPU_LAB_INDEXED") {
        if (!d.indexed_exchange) d.indexed_exchange = std::make_unique<IndexedNpuExchange>();
        indexed_exchange::Request request;
        request.header.width = s.width; request.header.height = s.height; request.header.vertices = count;
        request.header.format = s.format; request.header.flags = (UINT(d.blend.RenderTargetWriteMask[0]) << 8) | UINT(blend);
        if (textured) { request.header.abi = 2; request.header.flags |= 2; request.texture = std::move(texture); }
        if (clip != full) { request.header.abi = 3; request.header.flags |= 4; request.scissor = clip; }
        if (linear) {request.header.abi=4;request.header.flags|=8;}
        request.viewport = viewport; request.vertex = *d.vertex_program; request.pixel = *d.pixel_program;
        request.inputs = inputs; request.vertex_constants = vc; request.pixel_constants = pc; request.background = std::move(background);
        result = d.indexed_exchange->execute(std::move(request));
    } else if (linear) {
        const std::array<uint64_t,7> key{s.width,s.height,s.format,texture.width,texture.height,texture.format,uint64_t(blend)};
        if(!d.linear_texture_executor || d.linear_texture_vertex!=d.vertex_program || d.linear_texture_pixel!=d.pixel_program || d.linear_texture_key!=key){
            if (d.backend == "NPU") d.linear_texture_executor.reset();
            auto candidate=std::make_unique<nr::raster::LinearTextureDrawExecutor>(*d.vertex_program,*d.pixel_program,s.width,s.height,s.format,
                texture.width,texture.height,texture.format,d.backend,blend);
            d.linear_texture_executor=std::move(candidate);d.linear_texture_key=key;
            d.linear_texture_vertex=d.vertex_program;d.linear_texture_pixel=d.pixel_program;
        }
        result=d.linear_texture_executor->execute(inputs,vc,viewport,texture,background,clip);
    } else if (textured) {
        const std::array<uint64_t,6> key{s.width,s.height,s.format,texture.width,texture.height,texture.format};
        if (!d.point_texture_executor || d.point_texture_vertex != d.vertex_program || d.point_texture_pixel != d.pixel_program || d.point_texture_key != key) {
            if (d.backend == "NPU") d.point_texture_executor.reset();
            auto candidate = std::make_unique<nr::raster::PointTextureDrawExecutor>(*d.vertex_program,*d.pixel_program,s.width,s.height,s.format,
                texture.width,texture.height,texture.format,d.backend);
            d.point_texture_executor = std::move(candidate); d.point_texture_key = key;
            d.point_texture_vertex = d.vertex_program; d.point_texture_pixel = d.pixel_program;
        }
        result = d.point_texture_executor->execute(inputs,vc,viewport,texture);
    } else {
        const std::array<uint64_t, 5> key{s.width, s.height, s.format, count, uint64_t(d.blend.RenderTargetWriteMask[0]) | (uint64_t(blend) << 8)};
        if (!d.indexed_executor || d.indexed_vertex != d.vertex_program || d.indexed_pixel != d.pixel_program || d.indexed_key != key) {
            // Driver 5540 loses the new NPU vertex request if the old cache
            // entry is destroyed between new-model creation and first inference.
            // Retire the cache first. Pixels and current bindings stay intact if
            // compilation fails; a subsequent draw can rebuild the cache.
            if (d.backend == "NPU") d.indexed_executor.reset();
            uint32_t batch = 4096; while (uint64_t(s.width) * s.height % batch) batch /= 2;
            auto candidate = std::make_unique<nr::raster::UniformDrawExecutor>(*d.vertex_program, *d.pixel_program, count,
                s.width, s.height, d.backend, blend, s.format == 87, d.blend.RenderTargetWriteMask[0], batch);
            d.indexed_executor = std::move(candidate); d.indexed_key = key; d.indexed_vertex = d.vertex_program; d.indexed_pixel = d.pixel_program;
        }
        result = d.indexed_executor->execute(inputs, vc, pc, viewport, background, clip);
    }
    ++counters.executions;
    if (d.backend == "NPU_LAB_INDEXED") ++counters.remote_npu_executions;
    else if (d.backend == "NPU") ++counters.npu_executions;
    else if (d.backend == "GPU") ++counters.gpu_executions;
    else if (d.backend == "CPU") ++counters.cpu_executions;
    else throw Error{DXGI_ERROR_UNSUPPORTED};
    strncpy_s(counters.last_execution_device, result.execution_device.c_str(), _TRUNCATE);
    counters.inference_input_bytes += result.input_bytes;
    counters.inference_output_bytes += result.output_bytes;
    upload(*target, nullptr, result.pixels.data(), s.width * 4); ++counters.draws;
    lab_trace("{\"event\":\"%s\",\"vertices\":%u,\"textured\":%s,\"covered_samples\":%llu,\"rectangle_path\":%s,\"execution_device\":\"%s\",\"wall_ms\":%llu}\n",
        indexed ? "indexed_draw_executed" : "draw_executed", count, textured ? "true" : "false", static_cast<unsigned long long>(result.covered_samples), result.used_rectangle ? "true" : "false", result.execution_device.c_str(), static_cast<unsigned long long>(GetTickCount64()-started));
}
void execute_indexed_draw(Device &d, UINT count, UINT first, INT base) {
    if (!count) return;
    if (!d.vertex_program || !d.pixel_program) throw Error{DXGI_ERROR_UNSUPPORTED};
    require(d.topology == D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    execute_triangle_draw(d, fetch_indexed_vertices(d, count, first, base), true);
}
