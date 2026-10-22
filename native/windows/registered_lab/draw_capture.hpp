// Included after read_pixels. One bounded, read-only snapshot per guest process.
// NPDRAW01: repeated {char name[32], uint64 little-endian bytes, payload} records.
// State payloads use the locked x64 WDK D3D10 DDI ABI. Buffer payloads are raw;
// Texture payloads are tightly packed in their surface's logical format.
// A final zero-length "complete" record is required for a replayable snapshot.
class DrawCapture {
    HANDLE file = INVALID_HANDLE_VALUE;
    uint64_t written_bytes = 0;
public:
    static constexpr uint64_t budget = 96ull * 1024 * 1024;
    explicit DrawCapture(const WCHAR *path) {
        file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~DrawCapture() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    DrawCapture(const DrawCapture &) = delete;
    DrawCapture &operator=(const DrawCapture &) = delete;
    bool opened() const { return file != INVALID_HANDLE_VALUE; }
    void write(const void *data, uint64_t bytes) {
        require(bytes <= budget - written_bytes && bytes <= MAXDWORD && (!bytes || data));
        if (!bytes) return;
        DWORD written = 0;
        if (!WriteFile(file, data, DWORD(bytes), &written, nullptr) || written != bytes) throw Error{E_FAIL};
        written_bytes += bytes;
    }
    void record(const char *name, const void *data, uint64_t bytes) {
        char label[32]{}; require(strlen(name) < sizeof(label)); strcpy_s(label, name);
        write(label, sizeof(label)); write(&bytes, sizeof(bytes)); write(data, bytes);
    }
    template<class T> void value(const char *name, const T &data) { record(name, &data, sizeof(data)); }
};
void lab_capture_indexed_draw(Device &d, UINT count, UINT first, INT base, bool indexed = true, UINT instances=1, UINT first_instance=0) noexcept {
    if (!lab_trace_enabled()) return;
    static bool attempted = false; // Called under the UMD device mutex.
    if (attempted) return;
    attempted = true;
    WCHAR path[MAX_PATH]{};
    swprintf_s(path, NR_LAB_TRACE_ROOT L"\\draw-%lu.bin", GetCurrentProcessId());
    // CREATE_NEW survives DLL unload/reload and bounds repeated device failures.
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    const char *capture_phase="preflight";
    try {
        lab_trace("{\"event\":\"failed_draw_metadata\",\"count\":%u,\"first\":%u,\"base\":%d,\"target_width\":%u,\"target_height\":%u,\"source_width\":%u,\"source_height\":%u,\"viewport_width\":%.3f,\"viewport_height\":%.3f,\"capture_budget_bytes\":%llu}\n",
            count,first,base,d.bound_target?d.bound_target->surface.width:0,d.bound_target?d.bound_target->surface.height:0,
            d.bound_source?d.bound_source->surface.width:0,d.bound_source?d.bound_source->surface.height:0,
            double(d.viewport.Width),double(d.viewport.Height),static_cast<unsigned long long>(DrawCapture::budget));
        require(d.context && !d.context->poisoned);
        uint64_t bytes = 1024 * 1024; // Headers, state and two maximum-size shaders.
        auto buffer_size = [&](const std::shared_ptr<Allocation> &a, uint64_t size) {
            if (!a) return;
            require(a->context == d.context && !a->locked && size <= a->surface.allocation_bytes);
            require(size <= DrawCapture::budget - bytes); bytes += size;
        };
        buffer_size(d.index_buffer.allocation, d.index_buffer.bytes);
        for (const auto &b : d.vertex_buffers) buffer_size(b.allocation, b.bytes);
        for (const auto stage : {0u, 2u}) for (const auto &a : d.constant_buffers[stage])
            if (a) buffer_size(a, a->surface.allocation_bytes);
        for (const auto &a : {d.bound_source, d.bound_target})
            if (a) buffer_size(a, uint64_t(a->surface.width) * a->surface.height * 4);
        if(d.pixel_program)for(unsigned slot=0;slot<16;++slot)if(d.pixel_program->textures[slot]){
            const auto &view=d.shader_sources[2][slot];const auto &a=view.allocation;
            if(a)buffer_size(a,view.buffer?a->surface.allocation_bytes:uint64_t(a->surface.width)*a->surface.height*4);
        }
        capture_phase="serialize";
        DrawCapture capture(path); if (!capture.opened()) return;
        capture.write("NPDRAW01", 8);
        const uint32_t args[]{count, first, uint32_t(base), UINT(d.topology), d.viewport_count,
            d.vertex_shader, d.pixel_shader, d.sample_mask, d.stencil_ref};
        capture.value("arguments", args);
        const uint32_t call_kind = indexed ? 1u : 0u; capture.value("draw-indexed", call_kind);
        const uint32_t instance_args[]{instances,first_instance};capture.value("instances",instance_args);
        // Non-null depth/stencil views are rejected by targets().
        const uint32_t depth_target_bound=0;capture.value("depth-target-bound",depth_target_bound);
        capture.value("viewport", d.viewport); capture.value("blend", d.blend);
        capture.value("blend-factor", d.blend_factor); capture.value("depth", d.depth);
        capture.value("raster", d.raster);
        capture.record("scissors", d.scissors.data(), d.scissors.size() * sizeof(D3D10_DDI_RECT));
        if (d.input_elements) capture.record("layout", d.input_elements->data(),
            d.input_elements->size() * sizeof(D3D10DDIARG_INPUT_ELEMENT_DESC));
        const auto &vs = d.vertex_program ? d.vertex_program->bytecode : d.vertex_code;
        const auto &ps = d.pixel_program ? d.pixel_program->bytecode : d.pixel_code;
        capture.record("vertex-shader", vs.data(), vs.size() * sizeof(UINT));
        capture.record("pixel-shader", ps.data(), ps.size() * sizeof(UINT));
        auto raw = [&](const char *name, Allocation &a, uint64_t size) {
            Mapping read(a, true); capture.record(name, read.data, size); read.finish();
        };
        auto geometry = [&](const char *name, const Device::GeometryBinding &b) {
            if (!b.allocation) return;
            char label[32]{}; sprintf_s(label, "%s-description", name);
            const uint32_t description[]{b.bytes, b.stride, b.offset, UINT(b.format)};
            capture.value(label, description); raw(name, *b.allocation, b.bytes);
        };
        if (indexed) geometry("indices", d.index_buffer);
        for (UINT i = 0; i < d.vertex_buffers.size(); ++i) {
            char label[32]{}; sprintf_s(label, "vertices-%u", i); geometry(label, d.vertex_buffers[i]);
        }
        for (const auto stage : {0u, 2u}) {
            for (UINT i = 0; i < d.constant_buffers[stage].size(); ++i) {
                const auto &a = d.constant_buffers[stage][i]; if (!a) continue;
                char label[32]{}; sprintf_s(label, "constants-%u-%u", stage, i);
                raw(label, *a, a->surface.allocation_bytes);
            }
            for (UINT i = 0; i < d.samplers[stage].size(); ++i) {
                const auto &sampler = d.samplers[stage][i]; if (!sampler.bound) continue;
                char label[32]{}; sprintf_s(label, "sampler-%u-%u", stage, i); capture.value(label, sampler.desc);
            }
        }
        auto texture = [&](const char *name, const std::shared_ptr<Allocation> &a) {
            if (!a) return;
            char label[32]{}; sprintf_s(label, "%s-description", name); capture.value(label, a->surface);
            const auto pixels = read_pixels(*a); capture.record(name, pixels.data(), pixels.size());
        };
        texture("source", d.bound_source); texture("target", d.bound_target);
        if(d.pixel_program)for(unsigned slot=0;slot<16;++slot)if(d.pixel_program->textures[slot]){
            const auto &view=d.shader_sources[2][slot];const auto &a=view.allocation;if(!a)continue;
            char label[32]{};sprintf_s(label,"resource-2-%u-view",slot);
            const uint32_t description[]{view.format,view.first_element,view.elements,view.buffer?1u:0u};capture.value(label,description);
            sprintf_s(label,"resource-2-%u",slot);
            if(view.buffer){char surface_label[32]{};sprintf_s(surface_label,"resource-2-%u-description",slot);capture.value(surface_label,a->surface);raw(label,*a,a->surface.allocation_bytes);}
            else texture(label,a);
        }
        capture.record("complete", nullptr, 0);
        lab_trace("{\"event\":\"indexed_draw_capture\",\"count\":%u,\"first\":%u,\"base\":%d,\"indexed\":%s,\"complete\":true,\"execution_performed\":false}\n", count, first, base, indexed ? "true" : "false");
    } catch (...) {
        lab_trace("{\"event\":\"indexed_draw_capture\",\"complete\":false,\"phase\":\"%s\",\"execution_performed\":false}\n",capture_phase);
    }
}
