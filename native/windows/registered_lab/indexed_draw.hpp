// Included after diagnostic capture. Decode owned state, execute a checked
// graphics pipeline, then commit to VidMm only after the whole draw succeeds.
std::vector<nr::shader::VertexRegisters> fetch_indexed_vertices(Device &d, UINT count, UINT first, INT base) {
    const auto &ib = d.index_buffer;
    require(d.input_elements && count && count <= 65536 && !(count % 3) && ib.allocation &&
        ib.allocation->context == d.context && !ib.allocation->locked && !d.context->poisoned &&
        (ib.stride == 2 || ib.stride == 4) && uint64_t(ib.offset) + (uint64_t(first) + count) * ib.stride <= ib.bytes);
    std::vector<UINT> indices(count);
    {
        Mapping read(*ib.allocation, true);
        for (UINT i = 0; i < count; ++i) {
            UINT index = 0; std::memcpy(&index, static_cast<const uint8_t *>(read.data) + ib.offset + (uint64_t(first) + i) * ib.stride, ib.stride);
            const int64_t adjusted = int64_t(index) + base;
            require(adjusted >= 0 && adjusted <= UINT_MAX); indices[i] = UINT(adjusted);
        }
        read.finish();
    }
    std::vector<nr::shader::VertexRegisters> result(count);
    for (const auto &e : *d.input_elements) {
        const auto format = element_format(e.Format); const auto &b = d.vertex_buffers[e.InputSlot];
        require(b.allocation && b.allocation->context == d.context && !b.allocation->locked);
        for (const auto index : indices) require(uint64_t(b.offset) + e.AlignedByteOffset + uint64_t(index) * b.stride + format.bytes <= b.bytes);
        Mapping read(*b.allocation, true);
        for (UINT i = 0; i < count; ++i) {
            auto &v = result[i][e.InputRegister]; v = {0, 0, 0, format.integer ? 1u : 0x3f800000u};
            const auto *data = static_cast<const uint8_t *>(read.data) + b.offset + e.AlignedByteOffset + uint64_t(indices[i]) * b.stride;
            if (format.normalized) for (UINT c = 0; c < 4; ++c) { const float f = float(data[c]) / 255; std::memcpy(&v[c], &f, 4); }
            else if (format.half) for (UINT c = 0; c < format.lanes; ++c) { uint16_t f; std::memcpy(&f, data + c * 2, 2); v[c] = half_to_float_bits(f); }
            else std::memcpy(v.data(), data, format.bytes);
        }
        read.finish();
    }
    return result;
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
void execute_indexed_draw(Device &d, UINT count, UINT first, INT base) {
    if (!d.vertex_program || !d.pixel_program) throw Error{DXGI_ERROR_UNSUPPORTED};
    const auto target = d.bound_target;
    require(target && target->context == d.context && !target->locked && !d.context->poisoned &&
        d.topology == D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST && d.vertex_shader == 1 && d.pixel_shader == 2);
    const auto &s = target->surface;
    require((s.format == 28 || s.format == 87) && d.viewport_count == 1 && d.viewport.TopLeftX == 0 && d.viewport.TopLeftY == 0 &&
        d.viewport.Width == FLOAT(s.width) && d.viewport.Height == FLOAT(s.height) &&
        !d.depth.DepthEnable && !d.depth.StencilEnable && d.raster.FillMode == D3D10_DDI_FILL_SOLID &&
        d.raster.CullMode == D3D10_DDI_CULL_NONE && !d.raster.MultisampleEnable && !d.raster.AntialiasedLineEnable &&
        !d.raster.DepthBias && d.raster.DepthBiasClamp == 0 && d.raster.SlopeScaledDepthBias == 0 &&
        !d.blend.AlphaToCoverageEnable && d.sample_mask == ~0u && d.blend.RenderTargetWriteMask[0] && d.blend.RenderTargetWriteMask[0] <= 15);
    if (d.raster.ScissorEnable) require(d.scissors.size() == 1 && d.scissors[0].left == 0 && d.scissors[0].top == 0 &&
        d.scissors[0].right == LONG(s.width) && d.scissors[0].bottom == LONG(s.height));
    const bool blend = d.blend.BlendEnable[0] != 0;
    if (blend) require(d.blend.SrcBlend == D3D10_DDI_BLEND_ONE && d.blend.DestBlend == D3D10_DDI_BLEND_INV_SRC_ALPHA &&
        d.blend.BlendOp == D3D10_DDI_BLEND_OP_ADD && d.blend.SrcBlendAlpha == D3D10_DDI_BLEND_ONE &&
        d.blend.DestBlendAlpha == D3D10_DDI_BLEND_INV_SRC_ALPHA && d.blend.BlendOpAlpha == D3D10_DDI_BLEND_OP_ADD);
    const auto inputs = fetch_indexed_vertices(d, count, first, base);
    const auto vc = fetch_constants(d, 0, *d.vertex_program), pc = fetch_constants(d, 2, *d.pixel_program);
    const auto bytes = read_pixels(*target); std::vector<nr::raster::PackedColor> background(bytes.size() / 4);
    std::memcpy(background.data(), bytes.data(), bytes.size());
    const nr::raster::Viewport viewport{d.viewport.TopLeftX, d.viewport.TopLeftY, d.viewport.Width, d.viewport.Height, d.viewport.MinDepth, d.viewport.MaxDepth};
    nr::raster::UniformDrawExecutor::Result result;
    if (d.backend == "NPU_LAB_INDEXED") {
        if (!d.indexed_exchange) d.indexed_exchange = std::make_unique<IndexedNpuExchange>();
        indexed_exchange::Request request;
        request.header.width = s.width; request.header.height = s.height; request.header.vertices = count;
        request.header.format = s.format; request.header.flags = (UINT(d.blend.RenderTargetWriteMask[0]) << 8) | UINT(blend);
        request.viewport = viewport; request.vertex = *d.vertex_program; request.pixel = *d.pixel_program;
        request.inputs = inputs; request.vertex_constants = vc; request.pixel_constants = pc; request.background = std::move(background);
        result = d.indexed_exchange->execute(std::move(request));
    } else {
        const std::array<uint64_t, 5> key{s.width, s.height, s.format, count, uint64_t(d.blend.RenderTargetWriteMask[0]) | (uint64_t(blend) << 8)};
        if (!d.indexed_executor || d.indexed_vertex != d.vertex_program || d.indexed_pixel != d.pixel_program || d.indexed_key != key) {
            uint32_t batch = 4096; while (uint64_t(s.width) * s.height % batch) batch /= 2;
            auto candidate = std::make_unique<nr::raster::UniformDrawExecutor>(*d.vertex_program, *d.pixel_program, count,
                s.width, s.height, d.backend, blend, s.format == 87, d.blend.RenderTargetWriteMask[0], batch);
            d.indexed_executor = std::move(candidate); d.indexed_key = key; d.indexed_vertex = d.vertex_program; d.indexed_pixel = d.pixel_program;
        }
        result = d.indexed_executor->execute(inputs, vc, pc, viewport, background);
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
    lab_trace("{\"event\":\"indexed_draw_executed\",\"vertices\":%u,\"covered_samples\":%llu,\"rectangle_path\":%s,\"execution_device\":\"%s\",\"draw_ms\":%.3f}\n",
        count, static_cast<unsigned long long>(result.covered_samples), result.used_rectangle ? "true" : "false", result.execution_device.c_str(), result.timings.total_ms);
}
