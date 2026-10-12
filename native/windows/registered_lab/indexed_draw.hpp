// Decode IA inputs separately from shader arithmetic. Vertex IDs follow the
// D3D10/11 system-value contract: draw-local ordinal or raw index, without the
// vertex-buffer start/base address offset (functional spec sections 8.4/8.6).
std::vector<nr::shader::VertexRegisters> fetch_vertices_at(Device &d, const std::vector<UINT> &indices,
    const std::vector<UINT> &vertex_ids, UINT instances=1, UINT first_instance=0) {
    require(d.context && !d.context->poisoned && !indices.empty() && instances &&
        uint64_t(indices.size())*instances<=65536 && indices.size()==vertex_ids.size());
    const UINT builtin=d.vertex_program?d.vertex_program->vertex_id_input:16;
    const UINT instance_builtin=d.vertex_program?d.vertex_program->instance_id_input:16;
    std::vector<nr::shader::VertexRegisters> result(indices.size()*instances);
    uint32_t bound=0;
    if(d.input_elements)for(const auto &e:*d.input_elements){
        require(e.InputRegister<16 && e.InputRegister!=builtin && e.InputRegister!=instance_builtin && e.InputSlot<d.vertex_buffers.size());
        bound|=1u<<e.InputRegister;
        const auto format=element_format(e.Format);const auto &b=d.vertex_buffers[e.InputSlot];
        require(b.allocation && b.allocation->context==d.context && !b.allocation->locked);
        auto address=[&](size_t n){
            const auto instance=n/indices.size();
            const uint64_t index=e.InputSlotClass==D3D10_DDI_INPUT_PER_INSTANCE_DATA?
                uint64_t(first_instance)+(e.InstanceDataStepRate?instance/e.InstanceDataStepRate:0):indices[n%indices.size()];
            require(index<=UINT_MAX);
            return uint64_t(b.offset)+e.AlignedByteOffset+index*b.stride;
        };
        for(size_t n=0;n<result.size();++n)require(address(n)+format.bytes<=b.bytes);
        Mapping read(*b.allocation,true);
        for(size_t n=0;n<result.size();++n)decode_element(result[n][e.InputRegister],
            static_cast<const uint8_t *>(read.data)+size_t(address(n)),format);
        read.finish();
    }
    if(d.vertex_program)for(UINT reg=0;reg<16;++reg)
        require(!d.vertex_program->inputs[reg] || reg==builtin || reg==instance_builtin || (bound&(1u<<reg)));
    for(size_t n=0;n<result.size();++n){
        if(builtin<16)result[n][builtin][0]=vertex_ids[n%indices.size()];
        if(instance_builtin<16)result[n][instance_builtin][0]=UINT(n/indices.size());
    }
    return result;
}
std::vector<nr::shader::VertexRegisters> fetch_indexed_vertices(Device &d, UINT count, UINT first, INT base, UINT instances=1, UINT first_instance=0) {
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
    return fetch_vertices_at(d, indices, ids, instances, first_instance);
}
std::vector<nr::shader::VertexRegisters> fetch_draw_vertices(Device &d, UINT count, UINT first, UINT instances=1, UINT first_instance=0) {
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
    return fetch_vertices_at(d, indices, ids, instances, first_instance);
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
    // targets() rejects every non-null depth/stencil view. Without that
    // buffer, Direct3D depth and stencil tests always pass, even when enabled.
    const auto &s = target->surface;
    require((s.format == 28 || s.format == 87) && d.viewport_count == 1 && d.viewport.TopLeftX == 0 && d.viewport.TopLeftY == 0 &&
        d.viewport.Width == FLOAT(s.width) && d.viewport.Height == FLOAT(s.height) &&
        d.raster.FillMode == D3D10_DDI_FILL_SOLID &&
        d.raster.CullMode >= D3D10_DDI_CULL_NONE && d.raster.CullMode <= D3D10_DDI_CULL_BACK && !d.raster.MultisampleEnable && !d.raster.AntialiasedLineEnable &&
        !d.raster.DepthBias && d.raster.DepthBiasClamp == 0 && d.raster.SlopeScaledDepthBias == 0 &&
        !d.blend.AlphaToCoverageEnable && d.sample_mask == ~0u && d.blend.RenderTargetWriteMask[0] && d.blend.RenderTargetWriteMask[0] <= 15);
    const std::array<int32_t,4> full{0,0,int32_t(s.width),int32_t(s.height)};
    auto clip = full;
    if (d.raster.ScissorEnable) {
        require(d.scissors.size() == 1); const auto &r = d.scissors[0]; clip = {r.left,r.top,r.right,r.bottom};
        require(clip[0] >= 0 && clip[1] >= 0 && clip[0] <= clip[2] && clip[1] <= clip[3] && clip[2] <= full[2] && clip[3] <= full[3]);
    }
    nr::raster::BlendMode blend_mode=nr::raster::BlendMode::source_copy;
    if(d.blend.BlendEnable[0]){
        require(d.blend.BlendOp==D3D10_DDI_BLEND_OP_ADD && d.blend.BlendOpAlpha==D3D10_DDI_BLEND_OP_ADD &&
            d.blend.SrcBlend==d.blend.SrcBlendAlpha && d.blend.DestBlend==d.blend.DestBlendAlpha);
        constexpr std::array<D3D10_DDI_BLEND,6> factors{D3D10_DDI_BLEND_ZERO,D3D10_DDI_BLEND_ONE,D3D10_DDI_BLEND_SRC_ALPHA,
            D3D10_DDI_BLEND_INV_SRC_ALPHA,D3D10_DDI_BLEND_DEST_ALPHA,D3D10_DDI_BLEND_INV_DEST_ALPHA};
        bool found=false;
        for(uint32_t mode=0;mode<nr::raster::blend_factors.size();++mode){const auto pair=nr::raster::blend_factors[mode];
            if(d.blend.SrcBlend==factors[UINT(pair[0])] && d.blend.DestBlend==factors[UINT(pair[1])]){
                blend_mode=nr::raster::BlendMode(mode);found=true;break;
            }
        }
        require(found);
    }
    const bool blend=blend_mode!=nr::raster::BlendMode::source_copy;
    const bool destination_in=blend_mode==nr::raster::BlendMode::destination_in;
    const bool destination_out=blend_mode==nr::raster::BlendMode::destination_out;
    const bool general_blend=nr::raster::general_blend(blend_mode);
    const auto vc = fetch_constants(d, 0, *d.vertex_program), pc = fetch_constants(d, 2, *d.pixel_program);
    const bool textured = std::any_of(d.pixel_program->textures.begin(),d.pixel_program->textures.end(),[](bool v){return v;});
    const auto cull=static_cast<nr::raster::CullMode>(UINT(d.raster.CullMode)-1);
    const bool front_ccw=d.raster.FrontCounterClockwise!=0;
    nr::raster::PackedTexture texture; bool linear=false, modulated=false, general_texture=false;
    bool general_resources=nr::shader::quad_derivatives(*d.pixel_program);
    for(unsigned slot=0;slot<16;++slot)general_resources=general_resources ||
        (d.pixel_program->textures[slot] && (slot || d.pixel_program->texture_dimensions[slot]!=3 || d.pixel_program->texture_uint[slot])) ||
        (slot && d.pixel_program->samplers[slot]);
    for(const auto &instruction:d.pixel_program->instructions)general_resources=general_resources || instruction.opcode==nr::shader::Opcode::load;
    bool addressed=false;
    for(unsigned slot=0;slot<16;++slot)if(d.pixel_program->samplers[slot]){
        const auto &sampler=d.samplers[2][slot];require(sampler.bound);
        addressed=addressed || sampler.desc.AddressU!=D3D10_DDI_TEXTURE_ADDRESS_CLAMP || sampler.desc.AddressV!=D3D10_DDI_TEXTURE_ADDRESS_CLAMP;
    }
    general_resources=general_resources || addressed;
    nr::raster::PackedResources resources{};nr::shader::TextureSamplers resource_samplers{};
    if(general_resources){
        nr::raster::TriangleTextureDrawExecutor::validate_shader(*d.pixel_program);
        for(unsigned slot=0;slot<16;++slot){
            if(d.pixel_program->samplers[slot]){
                const auto &samp=d.samplers[2][slot];require(samp.bound &&
                    single_mip_filter_supported(UINT(samp.desc.Filter)) &&
                    (samp.desc.AddressU==D3D10_DDI_TEXTURE_ADDRESS_CLAMP || samp.desc.AddressU==D3D10_DDI_TEXTURE_ADDRESS_MIRROR) &&
                    (samp.desc.AddressV==D3D10_DDI_TEXTURE_ADDRESS_CLAMP || samp.desc.AddressV==D3D10_DDI_TEXTURE_ADDRESS_MIRROR) &&
                    samp.desc.AddressW==D3D10_DDI_TEXTURE_ADDRESS_CLAMP && samp.desc.MipLODBias==0 && samp.desc.MinLOD==0 && samp.desc.MaxLOD>=0);
                resource_samplers[slot].filter=single_mip_linear_filter(UINT(samp.desc.Filter))?nr::shader::TextureFilter::linear:nr::shader::TextureFilter::point;
                resource_samplers[slot].u=samp.desc.AddressU==D3D10_DDI_TEXTURE_ADDRESS_MIRROR?nr::shader::TextureAddress::mirror:nr::shader::TextureAddress::clamp;
                resource_samplers[slot].v=samp.desc.AddressV==D3D10_DDI_TEXTURE_ADDRESS_MIRROR?nr::shader::TextureAddress::mirror:nr::shader::TextureAddress::clamp;
            }
            if(!d.pixel_program->textures[slot])continue;
            const auto &view=d.shader_sources[2][slot];const auto source=view.allocation;if(!source)continue;
            require(source!=target && source->handle!=target->handle && source->context==d.context && !source->locked);
            auto &t=resources[slot];
            if(view.buffer){
                require(d.pixel_program->texture_dimensions[slot]==1 && d.pixel_program->texture_uint[slot] && view.format==62 && view.elements && view.elements<=4194304 &&
                    uint64_t(view.first_element)+view.elements<=source->surface.allocation_bytes);
                t={view.elements,1,42,{}};t.pixels.resize(view.elements);Mapping read(*source,true);
                const auto data=static_cast<const uint8_t *>(read.data)+view.first_element;
                for(size_t n=0;n<t.pixels.size();++n){const uint32_t value=data[n];std::memcpy(t.pixels[n].data(),&value,4);}read.finish();
            }else{
                const auto &ss=source->surface;require(d.pixel_program->texture_dimensions[slot]==3 && !d.pixel_program->texture_uint[slot] &&
                    (ss.format==28 || ss.format==87 || ss.format==65) && ss.width<=8192 && ss.height<=8192 && uint64_t(ss.width)*ss.height<=4194304);
                const auto data=read_pixels(*source);t={ss.width,ss.height,ss.format==65?28u:ss.format,{}};t.pixels.resize(size_t(ss.width)*ss.height);
                if(ss.format==65)for(size_t n=0;n<data.size();++n)t.pixels[n]={0,0,0,data[n]};else std::memcpy(t.pixels.data(),data.data(),data.size());
            }
        }
    }else if (textured) {
        const auto source = d.bound_source; const auto &sampler = d.samplers[2][0];
        linear=single_mip_linear_filter(UINT(sampler.desc.Filter));
        // A target larger than its source cannot be a one-to-one crop. The
        // affine point sampler handles scaling/clamping on the selected device.
        const bool source_smaller=source && (source->surface.width<s.width || source->surface.height<s.height);
        modulated=linear || d.pixel_program->instructions.size()==2 || blend || clip!=full || source_smaller;
        general_texture=general_blend || count!=6 || cull!=nr::raster::CullMode::none || d.blend.RenderTargetWriteMask[0]!=15;
        if(!general_texture) {
            try {
                if(modulated) nr::raster::LinearTextureDrawExecutor::validate_shader(*d.pixel_program);
                else nr::raster::PointTextureDrawExecutor::validate_shader(*d.pixel_program);
            } catch(const std::invalid_argument &) { general_texture=true; }
        }
        if(general_texture) nr::raster::TriangleTextureDrawExecutor::validate_shader(*d.pixel_program);
        require(source && source != target && source->handle != target->handle &&
            source->context == d.context && !source->locked && sampler.bound &&
            single_mip_filter_supported(UINT(sampler.desc.Filter)) &&
            sampler.desc.AddressU == D3D10_DDI_TEXTURE_ADDRESS_CLAMP && sampler.desc.AddressV == D3D10_DDI_TEXTURE_ADDRESS_CLAMP &&
            sampler.desc.AddressW == D3D10_DDI_TEXTURE_ADDRESS_CLAMP && sampler.desc.MipLODBias == 0 && sampler.desc.MinLOD == 0 && sampler.desc.MaxLOD >= 0);
        const auto &ss = source->surface;
        require((ss.format == 28 || ss.format == 87 || ss.format == 65) && ss.width <= 8192 && ss.height <= 8192 && uint64_t(ss.width)*ss.height <= 4*1024*1024);
        const auto source_bytes = read_pixels(*source); texture = {ss.width,ss.height,ss.format == 65 ? 28u : ss.format,{}};
        texture.pixels.resize(size_t(ss.width)*ss.height);
        // Format decoding only: sampling, shader math and blending remain on
        // the selected provider. Missing A8 RGB channels are zero.
        if(ss.format == 65) for(size_t i=0;i<source_bytes.size();++i) texture.pixels[i]={0,0,0,source_bytes[i]};
        else std::memcpy(texture.pixels.data(),source_bytes.data(),source_bytes.size());
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
        if (textured && !general_resources) { request.header.abi = 2; request.header.flags |= 2; request.texture = std::move(texture); }
        if (clip != full) { request.header.abi = 3; request.header.flags |= 4; request.scissor = clip; }
        if (modulated) {request.header.abi=linear?4:5;request.header.flags|=linear?8:16;}
        if(cull!=nr::raster::CullMode::none){request.header.abi=6;request.header.flags|=(UINT(cull)<<5)|(front_ccw?128u:0u);}
        if(general_texture){request.header.abi=7;request.header.flags|=0x1000|(linear?8u:16u);}
        if(general_resources){request.header.abi=8;request.header.flags|=0x2002;request.resources=std::move(resources);request.samplers=resource_samplers;}
        if(destination_in){request.header.abi=9;request.header.flags|=0x4000;}
        if(destination_out){request.header.abi=10;request.header.flags|=0x8000;}
        if(nr::raster::extended_blend(blend_mode)){request.header.abi=11;request.header.flags|=0x10000|(uint32_t(blend_mode)<<17);}
        if(addressed)request.header.abi=12;
        request.viewport = viewport; request.vertex = *d.vertex_program; request.pixel = *d.pixel_program;
        request.inputs = inputs; request.vertex_constants = vc; request.pixel_constants = pc; request.background = std::move(background);
        result = d.indexed_exchange->execute(std::move(request));
    } else if(general_resources){
        const uint64_t options=(uint64_t(blend_mode)<<12)|uint64_t(d.blend.RenderTargetWriteMask[0])|(uint64_t(blend)<<4)|(uint64_t(cull)<<6)|(uint64_t(front_ccw)<<8);
        const std::array<uint64_t,5> key{s.width,s.height,s.format,count,options};
        if(!d.resources_executor || d.resources_vertex!=d.vertex_program || d.resources_pixel!=d.pixel_program || d.resources_key!=key ||
            !nr::raster::resource_layout_equal(resources,d.resources_layout) || !nr::raster::sampler_layout_equal(resource_samplers,d.resources_samplers)){
            if(d.backend=="NPU")d.resources_executor.reset();constexpr uint32_t batch=4096;
            auto candidate=std::make_unique<nr::raster::TriangleTextureDrawExecutor>(*d.vertex_program,*d.pixel_program,count,s.width,s.height,s.format,
                resources,resource_samplers,d.backend,blend_mode,d.blend.RenderTargetWriteMask[0],cull,front_ccw,batch);
            d.resources_executor=std::move(candidate);d.resources_key=key;d.resources_layout=nr::raster::resource_layout(resources);d.resources_samplers=resource_samplers;
            d.resources_vertex=d.vertex_program;d.resources_pixel=d.pixel_program;
        }
        result=d.resources_executor->execute(inputs,vc,pc,viewport,resources,background,clip);
    } else if(general_texture) {
        const uint64_t options=(uint64_t(blend_mode)<<12)|uint64_t(d.blend.RenderTargetWriteMask[0])|(uint64_t(blend)<<4)|(uint64_t(linear)<<5)|(uint64_t(cull)<<6)|(uint64_t(front_ccw)<<8);
        const std::array<uint64_t,8> key{s.width,s.height,s.format,texture.width,texture.height,texture.format,count,options};
        if(!d.triangle_texture_executor || d.triangle_texture_vertex!=d.vertex_program || d.triangle_texture_pixel!=d.pixel_program || d.triangle_texture_key!=key){
            if(d.backend=="NPU")d.triangle_texture_executor.reset();
            constexpr uint32_t batch=4096;
            auto candidate=std::make_unique<nr::raster::TriangleTextureDrawExecutor>(*d.vertex_program,*d.pixel_program,count,s.width,s.height,s.format,
                texture.width,texture.height,texture.format,d.backend,blend_mode,d.blend.RenderTargetWriteMask[0],
                linear?nr::shader::TextureFilter::linear:nr::shader::TextureFilter::point,cull,front_ccw,batch);
            d.triangle_texture_executor=std::move(candidate);d.triangle_texture_key=key;
            d.triangle_texture_vertex=d.vertex_program;d.triangle_texture_pixel=d.pixel_program;
        }
        result=d.triangle_texture_executor->execute(inputs,vc,pc,viewport,texture,background,clip);
    } else if (modulated) {
        const std::array<uint64_t,8> key{s.width,s.height,s.format,texture.width,texture.height,texture.format,uint64_t(blend),uint64_t(linear)};
        if(!d.linear_texture_executor || d.linear_texture_vertex!=d.vertex_program || d.linear_texture_pixel!=d.pixel_program || d.linear_texture_key!=key){
            if (d.backend == "NPU") d.linear_texture_executor.reset();
            auto candidate=std::make_unique<nr::raster::LinearTextureDrawExecutor>(*d.vertex_program,*d.pixel_program,s.width,s.height,s.format,
                texture.width,texture.height,texture.format,d.backend,blend,linear ? nr::shader::TextureFilter::linear : nr::shader::TextureFilter::point);
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
        result = d.point_texture_executor->execute_adaptive(inputs,vc,viewport,texture,background);
    } else {
        const std::array<uint64_t, 5> key{s.width, s.height, s.format, count, uint64_t(d.blend.RenderTargetWriteMask[0]) | (uint64_t(blend) << 8) | (uint64_t(cull)<<9) | (uint64_t(front_ccw)<<11) | (uint64_t(blend_mode)<<12)};
        if (!d.indexed_executor || d.indexed_vertex != d.vertex_program || d.indexed_pixel != d.pixel_program || d.indexed_key != key) {
            // Driver 5540 loses the new NPU vertex request if the old cache
            // entry is destroyed between new-model creation and first inference.
            // Retire the cache first. Pixels and current bindings stay intact if
            // compilation fails; a subsequent draw can rebuild the cache.
            if (d.backend == "NPU") d.indexed_executor.reset();
            uint32_t batch = 4096; while (uint64_t(s.width) * s.height % batch) batch /= 2;
            auto candidate = std::make_unique<nr::raster::UniformDrawExecutor>(*d.vertex_program, *d.pixel_program, count,
                s.width, s.height, d.backend, blend_mode, s.format == 87, d.blend.RenderTargetWriteMask[0], batch, cull, front_ccw);
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
