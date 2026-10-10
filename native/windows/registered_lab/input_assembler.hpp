// Host decoding for the explicitly supported input formats. This prepares
// shader tensors; it neither executes shaders nor rasterizes primitives.
struct ElementFormat { UINT bytes, lanes; bool integer, normalized; };
ElementFormat element_format(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32_FLOAT: return {4, 1, false, false};
    case DXGI_FORMAT_R32G32_FLOAT: return {8, 2, false, false};
    case DXGI_FORMAT_R32G32B32_FLOAT: return {12, 3, false, false};
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return {16, 4, false, false};
    case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT: return {4, 1, true, false};
    case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT: return {8, 2, true, false};
    case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT: return {12, 3, true, false};
    case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: return {16, 4, true, false};
    case DXGI_FORMAT_R8G8B8A8_UNORM: return {4, 4, false, true};
    default: throw Error{DXGI_ERROR_UNSUPPORTED};
    }
}
SIZE_T APIENTRY layout_size(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATEELEMENTLAYOUT *) { return sizeof(ElementLayout); }
void APIENTRY create_layout(D3D10DDI_HDEVICE h, const D3D10DDIARG_CREATEELEMENTLAYOUT *args,
    D3D10DDI_HELEMENTLAYOUT value, D3D10DDI_HRTELEMENTLAYOUT) {
    new (value.pDrvPrivate) ElementLayout();
    guard(h, __FUNCTION__, [&] {
        require(args && args->NumElements <= 16 && (!args->NumElements || args->pVertexElements));
        auto elements = std::make_shared<InputElements>();
        UINT registers = 0;
        for (UINT i = 0; i < args->NumElements; ++i) {
            const auto &e = args->pVertexElements[i];
            lab_trace("{\"event\":\"input_element\",\"slot\":%u,\"offset\":%u,\"format\":%u,\"classification\":%u,\"step\":%u,\"register\":%u}\n",
                      e.InputSlot, e.AlignedByteOffset, UINT(e.Format), UINT(e.InputSlotClass), e.InstanceDataStepRate, e.InputRegister);
            require(e.InputRegister < 16 && e.InputSlot < 16 && !(registers & (1u << e.InputRegister)) &&
                    e.InputSlotClass == D3D10_DDI_INPUT_PER_VERTEX_DATA && !e.InstanceDataStepRate);
            const auto format = element_format(e.Format);
            require(e.AlignedByteOffset <= 2048 - format.bytes && (format.normalized || !(e.AlignedByteOffset % 4)));
            registers |= 1u << e.InputRegister; elements->push_back(e);
        }
        static_cast<ElementLayout *>(value.pDrvPrivate)->elements = std::move(elements);
    });
}
void APIENTRY destroy_layout(D3D10DDI_HDEVICE h, D3D10DDI_HELEMENTLAYOUT value) {
    guard(h, __FUNCTION__, [&] { static_cast<ElementLayout *>(value.pDrvPrivate)->~ElementLayout(); });
}
void APIENTRY set_layout(D3D10DDI_HDEVICE h, D3D10DDI_HELEMENTLAYOUT value) {
    guard(h, __FUNCTION__, [&] {
        const auto elements = value.pDrvPrivate ? static_cast<ElementLayout *>(value.pDrvPrivate)->elements : nullptr;
        require(!value.pDrvPrivate || bool(elements)); device(h).input_elements = elements;
    });
}
[[maybe_unused]] std::vector<nr::shader::VertexRegisters> fetch_vertices(Device &d, UINT count, UINT first) {
    require(d.input_elements && count && count <= 65536 && first <= UINT_MAX - (count - 1));
    std::vector<nr::shader::VertexRegisters> result(count);
    for (const auto &e : *d.input_elements) {
        const auto format = element_format(e.Format);
        const auto &binding = d.vertex_buffers[e.InputSlot];
        require(binding.allocation && binding.allocation->context == d.context &&
                !binding.allocation->locked && !d.context->poisoned);
        const uint64_t begin = uint64_t(binding.offset) + e.AlignedByteOffset + uint64_t(first) * binding.stride;
        const uint64_t end = begin + uint64_t(count - 1) * binding.stride + format.bytes;
        // Out-of-range vertex fetch semantics are not admitted by this first
        // bounded decoder. Reject before mapping instead of reading padding.
        require(end <= binding.bytes);
        Mapping read(*binding.allocation, true);
        for (UINT i = 0; i < count; ++i) {
            auto &v = result[i][e.InputRegister]; v = {0, 0, 0, format.integer ? 1u : 0x3f800000u};
            const auto *data = static_cast<const uint8_t *>(read.data) + size_t(begin + uint64_t(i) * binding.stride);
            if (format.normalized) {
                for (UINT c = 0; c < 4; ++c) {
                    const float component = float(data[c]) / 255.0f;
                    std::memcpy(&v[c], &component, sizeof(component));
                }
            } else std::memcpy(v.data(), data, format.bytes);
        }
        read.finish();
    }
    return result;
}
