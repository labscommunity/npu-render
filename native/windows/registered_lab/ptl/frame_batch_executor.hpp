#pragma once
// Worker-side frame batch execution: resident resources, draw planning, tile
// spans for the frame kernel, legacy per-draw fallback and the batch reply.
// Every pixel the reply carries was produced by the NPU: the frame kernel for
// eligible draws, the qualified per-draw routes for the rest.
#include "frame_batch_protocol.hpp"
#include "frame_batch_planner.hpp"
#include "frame_native_inventory.hpp"
#include "indexed_pipeline.hpp"
#include "npu_turbo_import.hpp"
#include <openvino/openvino.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <unordered_map>
namespace nr::registered_lab::frame_batch {
using indexed_exchange::require;
inline std::string sha256_hex(const void *data, size_t bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0);
    std::array<unsigned char, 32> digest{};
    const auto status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<void *>(data)), ULONG(bytes), digest.data(), ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0); require(status >= 0);
    constexpr char hex[] = "0123456789abcdef"; std::string result; result.reserve(64);
    for (const auto byte : digest) { result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
    return result;
}
// The twelve-row carrier with the frame kernel patched in. One request renders up to twelve spans.
class FrameKernelProvider {
    ov::Core core; ov::CompiledModel model; ov::InferRequest inference; std::string device; bool failed = false;
public:
    static constexpr unsigned rows = 12;
    uint64_t requests = 0;
    explicit FrameKernelProvider(const std::filesystem::path &blob, const std::string &expected_sha256) {
        std::ifstream file(blob, std::ios::binary); require(bool(file));
        std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        require(!bytes.empty() && sha256_hex(bytes.data(), bytes.size()) == expected_sha256);
        std::istringstream graph(bytes, std::ios::binary);
        model = nr::raster::import_ptl_turbo(core, graph, "frame");
        const auto devices = model.get_property(ov::execution_devices);
        require(devices.size() == 1 && (devices[0] == "NPU" || devices[0] == "NPU.0")); device = devices[0];
        inference = model.create_infer_request();
        // The Softmax carrier has one input; the ScatterElementsUpdate carrier adds the (unused) updates input.
        require((model.inputs().size() == 1 || model.inputs().size() == 2) && model.outputs().size() == 1 &&
                inference.get_input_tensor(0).get_element_type() == ov::element::f16 && inference.get_output_tensor().get_element_type() == ov::element::f16 &&
                inference.get_input_tensor(0).get_shape() == ov::Shape({rows, frame::span_bytes / 2}) && inference.get_output_tensor().get_shape() == ov::Shape({rows, frame::span_bytes / 2}));
        if (model.inputs().size() == 2) {
            auto updates = inference.get_input_tensor(1);
            std::memset(updates.data(), 0, updates.get_byte_size());
        }
        std::printf("{\"frame_kernel_provider_created\":true,\"rows\":%u,\"inputs\":%zu,\"execution_device\":\"%s\"}\n", rows, model.inputs().size(), device.c_str()); std::fflush(stdout);
    }
    const std::string &execution_device() const { return device; }
    // Writes the spans into the input tensor, runs the NPU and copies back each output span prefix.
    void execute(const std::vector<std::vector<uint8_t>> &spans, std::vector<std::vector<uint8_t>> &outputs) {
        require(!failed && !spans.empty() && spans.size() <= rows); failed = true;
        auto *input = static_cast<uint8_t *>(inference.get_input_tensor(0).data());
        for (size_t n = 0; n < spans.size(); ++n) { require(spans[n].size() == frame::span_bytes); std::memcpy(input + n * frame::span_bytes, spans[n].data(), frame::span_bytes); }
        for (size_t n = spans.size(); n < rows; ++n) std::memset(input + n * frame::span_bytes, 0, 64);   // empty rows fail the header check cheaply
        inference.start_async();
        if (!inference.wait_for(std::chrono::milliseconds(10000))) throw std::runtime_error("frame kernel timeout");
        ++requests;
        const auto *output = static_cast<const uint8_t *>(inference.get_output_tensor().data());
        outputs.resize(spans.size());
        for (size_t n = 0; n < spans.size(); ++n) {
            uint32_t tile_w, tile_h; std::memcpy(&tile_w, spans[n].data() + 16, 4); std::memcpy(&tile_h, spans[n].data() + 20, 4);
            const size_t live = NR_FRAME_OUTPUT_PIXELS + size_t(tile_w) * tile_h * 4;
            outputs[n].assign(output + n * frame::span_bytes, output + n * frame::span_bytes + live);
        }
        failed = false;
    }
};
struct ResidentResource { uint64_t key = 0, version = 0; uint32_t width = 0, height = 0, format = 0, flags = 0; std::vector<uint32_t> pixels; };
struct VolatileWindow { uint32_t x, y, width, height; std::vector<uint32_t> pixels; };
class FrameBatchExecutor {
    indexed_exchange::Pipeline &legacy;
    std::array<std::unique_ptr<FrameKernelProvider>, frame::blob_ranges.size()> kernels;
    std::unique_ptr<frame::NativeInventory> inventory;
    std::unordered_map<uint64_t, ResidentResource> resident;
    std::unordered_map<std::string, std::shared_ptr<const shader::Program>> shaders;   // sha256 hex -> parsed
    std::map<std::string, std::shared_ptr<const frame::LoweredProgram>> lowered;        // vs sha + ps sha + sampler bits
    uint64_t generation = 1;
    struct Shader { std::shared_ptr<const shader::Program> program; std::string sha; uint32_t stage; };
    struct ResourceRef { ResourceRecord record; ResidentResource *resident = nullptr; std::vector<VolatileWindow> windows; bool uploaded_full = false; };
    struct Draw {
        CommandRecord record; std::array<TextureBinding, 4> textures{}; std::vector<shader::VertexRegisters> inputs;
        std::vector<uint32_t> vertex_constants, pixel_constants;
        std::shared_ptr<const frame::LoweredProgram> program; frame::DrawPlan plan; frame::Rect scissor_bounds; bool eligible = false; std::string route;
        uint32_t blob = UINT32_MAX; std::vector<uint8_t> kernel_entry;   // program entry carrying the blob-local native index
        uint32_t tile = 128;                                             // tile edge the draw's texture windows fit at
        bool done = false;                                               // executed (kernel or fallback)
    };
    struct Stats { uint64_t kernel_requests = 0, covered = 0, escapes = 0, executed = 0, fallback = 0; double kernel_ms = 0, host_ms = 0; };
    const std::vector<Shader> *current_shaders = nullptr;   // shaders of the batch being executed
    static std::string sampler_key(const std::array<TextureBinding, 4> &t) {
        std::string key; for (const auto &b : t) key += std::to_string(b.filter) + ":" + std::to_string(b.address_u) + ":" + std::to_string(b.address_v) + ",";
        return key;
    }
    // Pixels of a resource rectangle as the kernel expects (packed, row-major).
    static void copy_rect(const uint32_t *source, uint32_t stride, frame::Rect r, std::vector<uint32_t> &out) {
        out.resize(size_t(r.x1 - r.x0) * (r.y1 - r.y0));
        for (int32_t y = r.y0; y < r.y1; ++y) std::memcpy(out.data() + size_t(y - r.y0) * (r.x1 - r.x0), source + size_t(y) * stride + r.x0, size_t(r.x1 - r.x0) * 4);
    }
    // Texels for a window of a possibly volatile resource; returns false when the uploaded windows do not cover it.
    static bool fetch_window(const ResourceRef &ref, frame::Rect r, std::vector<uint32_t> &out) {
        if (ref.resident && (!(ref.record.flags & volatile_resource) || ref.uploaded_full)) { copy_rect(ref.resident->pixels.data(), ref.resident->width, r, out); return true; }
        for (const auto &w : ref.windows) {
            if (r.x0 >= int32_t(w.x) && r.y0 >= int32_t(w.y) && r.x1 <= int32_t(w.x + w.width) && r.y1 <= int32_t(w.y + w.height)) {
                copy_rect(w.pixels.data(), w.width, {r.x0 - int32_t(w.x), r.y0 - int32_t(w.y), r.x1 - int32_t(w.x), r.y1 - int32_t(w.y)}, out); return true;
            }
        }
        return false;
    }
public:
    // `blob_dir` holds manifest.txt with one line per kernel blob: "<blob> <sha256> <file>".
    explicit FrameBatchExecutor(indexed_exchange::Pipeline &pipeline, const std::filesystem::path &blob_dir) : legacy(pipeline) {
        if (blob_dir.empty()) return;
        inventory = std::make_unique<frame::NativeInventory>();
        std::ifstream manifest(blob_dir / "manifest.txt"); require(bool(manifest));
        std::string line;
        while (std::getline(manifest, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream fields(line); unsigned blob = UINT32_MAX; std::string sha, file;
            require(bool(fields >> blob >> sha >> file) && blob < kernels.size() && sha.size() == 64 && !kernels[blob]);
            kernels[blob] = std::make_unique<FrameKernelProvider>(blob_dir / file, sha);
            std::printf("{\"frame_kernel_blob\":%u,\"file\":\"%s\"}\n", blob, file.c_str()); std::fflush(stdout);
        }
    }
    bool kernel_available() const { for (const auto &k : kernels) if (k) return true; return false; }
    uint64_t current_generation() const { return generation; }
    // Executes one batch packet and returns the complete reply packet (Header + body).
    std::vector<uint8_t> execute(const std::vector<uint8_t> &packet) {
        const auto started = std::chrono::steady_clock::now();
        indexed_exchange::Reader reader(packet); const auto header = reader.value<indexed_exchange::Header>();
        require(batch_header_valid(header) && packet.size() == sizeof(header) + header.body_bytes);
        const auto batch = reader.value<BatchHeader>();
        require(batch.magic == batch_magic && batch.version == 1 && batch.shader_count <= max_shaders && batch.resource_count <= max_resources &&
                batch.upload_count <= max_uploads && batch.command_count <= max_commands && !batch.reserved);
        Stats stats;
        // Shaders.
        std::vector<Shader> batch_shaders(batch.shader_count);
        for (auto &s : batch_shaders) {
            const auto record = reader.value<ShaderRecord>(); require((record.stage == 1 || record.stage == 2) && record.words >= 3 && record.words <= 16384);
            const auto words = reader.vector<uint32_t>(record.words);
            s.sha = sha256_hex(words.data(), words.size() * 4); s.stage = record.stage;
            char expected[65]{}; for (unsigned n = 0; n < 32; ++n) std::snprintf(expected + n * 2, 3, "%02x", record.sha256[n]);
            require(s.sha == expected);
            auto found = shaders.find(s.sha);
            if (found == shaders.end()) {
                auto program = std::make_shared<shader::Program>(record.stage == 1 ? shader::parse_vertex(words.data(), words.size()) : shader::parse_pixel(words.data(), words.size()));
                found = shaders.emplace(s.sha, std::move(program)).first;
            }
            s.program = found->second; require((s.program->stage == shader::Stage::vertex) == (record.stage == 1));
        }
        // Resources.
        std::vector<ResourceRef> refs(batch.resource_count);
        for (auto &ref : refs) {
            ref.record = reader.value<ResourceRecord>();
            require(ref.record.key && ref.record.width && ref.record.width <= 8192 && ref.record.height && ref.record.height <= 8192 &&
                    uint64_t(ref.record.width) * ref.record.height <= 8388608 && (ref.record.format == 28 || ref.record.format == 87));
            auto &entry = resident[ref.record.key];
            if (entry.width != ref.record.width || entry.height != ref.record.height || entry.format != ref.record.format) {
                entry = ResidentResource{ref.record.key, 0, ref.record.width, ref.record.height, ref.record.format, ref.record.flags, {}};
            }
            entry.flags = ref.record.flags; ref.resident = &entry;
        }
        // Uploads: full images replace the resident version; windows feed volatile sampling in this batch only.
        for (uint32_t n = 0; n < batch.upload_count; ++n) {
            const auto up = reader.value<UploadRecord>(); require(up.resource < refs.size() && !up.reserved);
            auto &ref = refs[up.resource];
            require(up.width && up.height && up.x <= ref.record.width && up.y <= ref.record.height && up.width <= ref.record.width - up.x && up.height <= ref.record.height - up.y);
            auto pixels = reader.vector<uint32_t>(size_t(up.width) * up.height);
            if (!up.x && !up.y && up.width == ref.record.width && up.height == ref.record.height) {
                ref.resident->pixels = std::move(pixels); ref.resident->version = ref.record.version; ref.uploaded_full = true;
            } else {
                require(ref.record.flags & volatile_resource);
                ref.windows.push_back({up.x, up.y, up.width, up.height, std::move(pixels)});
            }
        }
        // Commands.
        std::vector<Draw> draws(batch.command_count);
        for (auto &d : draws) {
            d.record = reader.value<CommandRecord>();
            require(d.record.kind <= 1 && d.record.target < refs.size() && d.record.texture_count <= 4 && d.record.blend_mode <= 13 && d.record.write_mask && d.record.write_mask <= 15 && d.record.cull <= 2 && d.record.front_ccw <= 1);
            for (uint32_t t = 0; t < d.record.texture_count; ++t) { d.textures[t] = reader.value<TextureBinding>(); require(d.textures[t].resource < refs.size() && d.textures[t].filter <= 1); }
            if (d.record.kind == 0) {
                require(d.record.vertex_shader < batch_shaders.size() && d.record.pixel_shader < batch_shaders.size() && batch_shaders[d.record.vertex_shader].stage == 1 && batch_shaders[d.record.pixel_shader].stage == 2);
                require(d.record.vertex_count && d.record.vertex_count % 3 == 0 && d.record.vertex_count <= 4095 && d.record.vertex_words == d.record.vertex_count * 64);
                d.inputs.resize(d.record.vertex_count);
                for (auto &v : d.inputs) for (auto &reg : v) for (auto &lane : reg) lane = reader.value<uint32_t>();
                d.vertex_constants = reader.vector<uint32_t>(d.record.vertex_constant_words); d.pixel_constants = reader.vector<uint32_t>(d.record.pixel_constant_words);
            } else require(!d.record.vertex_count && !d.record.vertex_words && !d.record.vertex_constant_words && !d.record.pixel_constant_words);
        }
        require(reader.done());
        // Missing versions: a private resource the batch references without the right content.
        std::vector<MissingRecord> missing;
        for (const auto &d : draws) {
            auto check = [&](uint32_t index) { const auto &ref = refs[index]; if (!(ref.record.flags & volatile_resource) && ref.resident->version != ref.record.version) missing.push_back({ref.record.key, ref.record.version}); };
            check(d.record.target);
            for (uint32_t t = 0; t < d.record.texture_count; ++t) check(d.textures[t].resource);
        }
        std::sort(missing.begin(), missing.end(), [](const auto &a, const auto &b) { return a.key < b.key || (a.key == b.key && a.version < b.version); });
        missing.erase(std::unique(missing.begin(), missing.end(), [](const auto &a, const auto &b) { return a.key == b.key && a.version == b.version; }), missing.end());
        std::map<uint64_t, frame::Rect> dirty;
        current_shaders = &batch_shaders;
        if (missing.empty()) {
            // Plan every draw, then execute runs in order.
            for (auto &d : draws) if (d.record.kind == 0) plan(d, batch_shaders, refs);
            std::vector<size_t> run; size_t run_target = SIZE_MAX; uint32_t run_blob = UINT32_MAX;
            auto flush = [&] { if (!run.empty()) execute_run(draws, run, refs, dirty, stats); run.clear(); run_target = SIZE_MAX; run_blob = UINT32_MAX; };
            for (size_t n = 0; n < draws.size(); ++n) {
                auto &d = draws[n];
                if (d.record.kind == 1) { flush(); clear(d, refs, dirty); ++stats.executed; continue; }
                bool hazard = false;
                for (uint32_t t = 0; t < d.record.texture_count; ++t) if (d.textures[t].resource == run_target) hazard = true;
                if (d.eligible) fit(d, refs);
                const bool breaks = !run.empty() && draws[run.back()].tile != d.tile;
                if (!d.eligible || hazard || breaks || (run_target != SIZE_MAX && run_target != d.record.target) || (run_blob != UINT32_MAX && run_blob != d.blob)) flush();
                if (!d.eligible) { fallback(d, batch_shaders, refs, dirty, stats); continue; }
                run_target = d.record.target; run_blob = d.blob; run.push_back(n);
            }
            flush();
        }
        stats.host_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        // Reply.
        indexed_exchange::Writer w; auto reply = header; reply.kind = 2; reply.device = 3;
        ResponseHeader response; response.target_count = uint32_t(dirty.size()); response.missing_count = uint32_t(missing.size()); response.generation = generation;
        response.batch_sequence = batch.batch_sequence; response.provider_requests = stats.kernel_requests; response.covered_samples = stats.covered;
        response.kernel_ms_x1000 = uint64_t(stats.kernel_ms * 1000); response.host_ms_x1000 = uint64_t(stats.host_ms * 1000);
        response.commands_executed = uint32_t(stats.executed); response.commands_fallback = uint32_t(stats.fallback); response.escapes = uint32_t(stats.escapes);
        if (!missing.empty()) response.target_count = 0;
        w.value(reply); w.value(response);
        if (missing.empty()) for (const auto &[key, rect] : dirty) {
            auto &r = resident.at(key); r.version = batch.batch_sequence;
            TargetRecord t{key, r.version, uint32_t(rect.x0), uint32_t(rect.y0), uint32_t(rect.x1 - rect.x0), uint32_t(rect.y1 - rect.y0)};
            w.value(t); std::vector<uint32_t> pixels; copy_rect(r.pixels.data(), r.width, rect, pixels); w.append(pixels.data(), pixels.size() * 4);
        } else for (const auto &m : missing) w.value(m);
        auto bytes = w.finish(); reply.body_bytes = uint32_t(bytes.size() - sizeof(reply)); std::memcpy(bytes.data(), &reply, sizeof(reply));
        std::printf("{\"frame_batch\":true,\"sequence\":%u,\"commands\":%u,\"executed\":%llu,\"fallback\":%llu,\"kernel_requests\":%llu,\"covered\":%llu,\"escapes\":%llu,\"missing\":%zu,\"targets\":%zu,\"kernel_ms\":%.3f,\"host_ms\":%.3f,\"execution_device\":\"NPU\"}\n",
            header.sequence, batch.command_count, (unsigned long long)stats.executed, (unsigned long long)stats.fallback, (unsigned long long)stats.kernel_requests, (unsigned long long)stats.covered, (unsigned long long)stats.escapes, missing.size(), dirty.size(), stats.kernel_ms, stats.host_ms);
        std::fflush(stdout);
        return bytes;
    }
private:
    void plan(Draw &d, const std::vector<Shader> &batch_shaders, const std::vector<ResourceRef> &refs) {
        const auto &vs = *batch_shaders[d.record.vertex_shader].program, &ps = *batch_shaders[d.record.pixel_shader].program;
        // Samplers are addressed by SM4 sampler slot inside lowering; bindings arrive per texture index in slot order.
        const std::string key = batch_shaders[d.record.vertex_shader].sha + batch_shaders[d.record.pixel_shader].sha + sampler_key(d.textures);
        auto found = lowered.find(key);
        if (found == lowered.end()) {
            shader::TextureSamplers s{};
            // Each pixel-shader sampler slot takes the binding of the texture index it is paired with after lowering;
            // lower once with clamp/linear defaults to learn the pairing, then re-lower with the real filters.
            auto first = frame::lower(vs, ps, s);
            if (first.eligible()) for (size_t t = 0; t < first.sampler_slots.size() && t < d.record.texture_count; ++t) {
                s[first.sampler_slots[t]].filter = d.textures[t].filter ? shader::TextureFilter::point : shader::TextureFilter::linear;
                s[first.sampler_slots[t]].u = shader::TextureAddress(d.textures[t].address_u); s[first.sampler_slots[t]].v = shader::TextureAddress(d.textures[t].address_v);
            }
            auto program = std::make_shared<frame::LoweredProgram>(frame::lower(vs, ps, s));
            if (program->eligible()) {
                const auto *identity = inventory ? inventory->find(*program) : nullptr;
                if (!identity) program->rejection = "not a compiled inventory program";
                else program->set_native(identity->host_index);
            }
            found = lowered.emplace(key, std::move(program)).first;
        }
        d.program = found->second;
        if (!d.program->eligible()) { d.route = "fallback:" + d.program->rejection; return; }
        {
            const auto *identity = inventory->find(*d.program); require(identity != nullptr);
            if (!kernels[identity->blob]) { d.route = "fallback:kernel blob " + std::to_string(identity->blob) + " unavailable"; return; }
            d.blob = identity->blob; d.kernel_entry = d.program->entry;
            std::memcpy(d.kernel_entry.data(), &identity->blob_index, 4);
        }
        if (d.program->texture_slots.size() != d.record.texture_count) { d.route = "fallback:texture count"; return; }
        if (d.record.vertex_constant_words != d.program->vs_constants * 4 || d.record.pixel_constant_words != d.program->ps_constants * 4) { d.route = "fallback:constant words"; return; }
        std::array<uint32_t, 4> tw{}, th{}; std::array<bool, 4> point{};
        for (uint32_t t = 0; t < d.record.texture_count; ++t) { const auto &ref = refs[d.textures[t].resource]; tw[t] = ref.record.width; th[t] = ref.record.height; point[t] = d.textures[t].filter != 0; }
        std::vector<uint32_t> constants(d.vertex_constants); constants.insert(constants.end(), d.pixel_constants.begin(), d.pixel_constants.end());
        d.plan = frame::plan_draw(*d.program, frame::pack_vertices(*d.program, d.inputs), d.record.vertex_count, constants,
                                  {d.record.viewport[0], d.record.viewport[1], d.record.viewport[2], d.record.viewport[3]}, tw, th, point);
        if (!d.plan.valid) { d.route = "fallback:" + d.plan.rejection; return; }
        const auto &target = refs[d.record.target].record;
        d.scissor_bounds = {0, 0, int32_t(target.width), int32_t(target.height)};
        if (d.record.scissor_enabled) d.scissor_bounds = frame::intersect(d.scissor_bounds, {d.record.scissor[0], d.record.scissor[1], d.record.scissor[2], d.record.scissor[3]});
        d.eligible = true; d.route = "frame";
    }
    // Bytes a tile span needs for one draw's texture windows plus fixed costs; chooses the tile edge.
    void fit(Draw &d, const std::vector<ResourceRef> &refs) {
        const auto &target = refs[d.record.target].record;
        const frame::Rect bounds = frame::intersect(frame::intersect(d.plan.bounds, d.scissor_bounds), {0, 0, int32_t(target.width), int32_t(target.height)});
        if (bounds.empty()) { d.tile = 128; return; }
        for (uint32_t edge : {128u, 64u, 32u}) {
            bool fits = true;
            for (int32_t ty = bounds.y0 / int32_t(edge) * int32_t(edge); ty < bounds.y1 && fits; ty += int32_t(edge)) for (int32_t tx = bounds.x0 / int32_t(edge) * int32_t(edge); tx < bounds.x1 && fits; tx += int32_t(edge)) {
                const frame::Rect clip = frame::intersect(bounds, {tx, ty, tx + int32_t(edge), ty + int32_t(edge)});
                if (clip.empty()) continue;
                size_t bytes = NR_FRAME_HEADER_BYTES + NR_FRAME_COMMAND_BYTES + d.kernel_entry.size() + 64 + d.record.vertex_words * 4 + (d.vertex_constants.size() + d.pixel_constants.size()) * 4 + 4 + edge * edge * 4;
                for (uint32_t t = 0; t < d.record.texture_count; ++t) {
                    const auto &tref = refs[d.textures[t].resource];
                    const auto r = frame::texel_window(*d.program, d.plan, clip, t, tref.record.width, tref.record.height, d.textures[t].filter != 0);
                    bytes += NR_FRAME_WINDOW_BYTES + (r.empty() ? 4 : size_t(r.x1 - r.x0) * size_t(r.y1 - r.y0) * 4);
                }
                if (bytes > frame::span_bytes - 1024) fits = false;
            }
            if (fits) { d.tile = edge; return; }
        }
        d.eligible = false; d.route = "fallback:span budget";
    }
    void clear(const Draw &d, std::vector<ResourceRef> &refs, std::map<uint64_t, frame::Rect> &dirty) {
        auto &ref = refs[d.record.target]; auto &res = *ref.resident;
        std::array<float, 4> color{d.record.clear_color[0], d.record.clear_color[1], d.record.clear_color[2], d.record.clear_color[3]};
        const uint32_t packed = frame::pack_clear_color(color, res.format);
        res.pixels.assign(size_t(res.width) * res.height, packed);
        dirty[ref.record.key] = frame::unite(dirty[ref.record.key], {0, 0, int32_t(res.width), int32_t(res.height)});
        std::printf("{\"frame_batch_clear\":true,\"target\":%llu}\n", (unsigned long long)ref.record.key); std::fflush(stdout);
    }
    void fallback(Draw &d, const std::vector<Shader> &batch_shaders, std::vector<ResourceRef> &refs, std::map<uint64_t, frame::Rect> &dirty, Stats &stats) {
        // Build the legacy per-draw request against the resident target and run the qualified route.
        auto &ref = refs[d.record.target]; auto &res = *ref.resident; require(res.pixels.size() == size_t(res.width) * res.height);
        indexed_exchange::Request request; auto &h = request.header;
        // legacy_abi packs the recorded abi (low 16 bits) and the reserved word that carries the payload abi for large variants.
        h.abi = d.record.legacy_abi & 0xffffu; h.reserved = d.record.legacy_abi >> 16; h.flags = d.record.legacy_flags; h.width = res.width; h.height = res.height; h.vertices = d.record.vertex_count; h.format = res.format;
        std::printf("{\"frame_batch_fallback_begin\":true,\"route\":\"%s\",\"abi\":%u,\"reserved\":%u,\"flags\":%u}\n", d.route.c_str(), h.abi, h.reserved, h.flags); std::fflush(stdout);
        h.sequence = 1; h.client_pid = GetCurrentProcessId(); h.nonce[0] = 1;
        request.vertex = *batch_shaders[d.record.vertex_shader].program; request.pixel = *batch_shaders[d.record.pixel_shader].program;
        h.vs_words = uint32_t(request.vertex.bytecode.size()); h.ps_words = uint32_t(request.pixel.bytecode.size());
        request.inputs = d.inputs;
        for (unsigned i = 0, at = 0; i < 14; ++i) { request.vertex_constants[i].resize(request.vertex.constants[i]); for (auto &v : request.vertex_constants[i]) for (auto &lane : v) lane = at < d.vertex_constants.size() ? d.vertex_constants[at++] : 0u; }
        for (unsigned i = 0, at = 0; i < 14; ++i) { request.pixel_constants[i].resize(request.pixel.constants[i]); for (auto &v : request.pixel_constants[i]) for (auto &lane : v) lane = at < d.pixel_constants.size() ? d.pixel_constants[at++] : 0u; }
        request.viewport = {d.record.viewport[0], d.record.viewport[1], d.record.viewport[2], d.record.viewport[3], d.record.viewport[4], d.record.viewport[5]};
        request.blend_constant = {d.record.blend_constant[0], d.record.blend_constant[1], d.record.blend_constant[2], d.record.blend_constant[3]};
        if (d.record.scissor_enabled) request.scissor = std::array<int32_t, 4>{d.record.scissor[0], d.record.scissor[1], d.record.scissor[2], d.record.scissor[3]};
        request.background.resize(res.pixels.size()); std::memcpy(request.background.data(), res.pixels.data(), res.pixels.size() * 4);
        // Textures: slot assignment follows the SM4 texture slots of the pixel program in binding order.
        std::vector<uint32_t> slots; for (unsigned slot = 0; slot < 16; ++slot) if (request.pixel.textures[slot]) slots.push_back(slot);
        require(slots.size() == d.record.texture_count);
        for (uint32_t t = 0; t < d.record.texture_count; ++t) {
            const auto &tref = refs[d.textures[t].resource]; require(tref.resident && (tref.uploaded_full || !(tref.record.flags & volatile_resource)) && tref.resident->pixels.size() == size_t(tref.resident->width) * tref.resident->height);
            raster::PackedTexture texture{tref.resident->width, tref.resident->height, tref.resident->format, {}};
            texture.pixels.resize(tref.resident->pixels.size()); std::memcpy(texture.pixels.data(), tref.resident->pixels.data(), tref.resident->pixels.size() * 4);
            if (h.flags & 0x2000) { request.resources[slots[t]] = std::move(texture); auto &s = request.samplers[slots[t]]; s.filter = d.textures[t].filter ? shader::TextureFilter::point : shader::TextureFilter::linear; s.u = shader::TextureAddress(d.textures[t].address_u); s.v = shader::TextureAddress(d.textures[t].address_v); for (unsigned c = 0; c < 4; ++c) s.border[c] = d.textures[t].border[c]; }
            else request.texture = std::move(texture);
        }
        raster::UniformDrawExecutor::Result result;
        try { result = legacy.execute(request); }
        catch (const std::exception &error) { throw std::runtime_error(std::string("frame batch fallback (") + d.route + ") failed: " + error.what()); }
        require(result.execution_device == "NPU" && result.pixels.size() == res.pixels.size());
        std::memcpy(res.pixels.data(), result.pixels.data(), res.pixels.size() * 4);
        dirty[ref.record.key] = frame::unite(dirty[ref.record.key], frame::intersect(d.plan.valid ? d.plan.bounds : frame::Rect{0, 0, int32_t(res.width), int32_t(res.height)}, {0, 0, int32_t(res.width), int32_t(res.height)}));
        if (!d.plan.valid) dirty[ref.record.key] = {0, 0, int32_t(res.width), int32_t(res.height)};
        ++stats.fallback; ++stats.executed; d.done = true;
        std::printf("{\"frame_batch_fallback\":true,\"route\":\"%s\",\"provider_requests\":%llu}\n", d.route.c_str(), (unsigned long long)result.provider_requests); std::fflush(stdout);
    }
    // Renders one ordered run of eligible draws on one target through the frame kernel.
    void execute_run(std::vector<Draw> &draws, const std::vector<size_t> &run, std::vector<ResourceRef> &refs, std::map<uint64_t, frame::Rect> &dirty, Stats &stats) {
        auto &ref = refs[draws[run.front()].record.target]; auto &res = *ref.resident; require(res.pixels.size() == size_t(res.width) * res.height);
        auto &kernel = kernels[draws[run.front()].blob]; require(bool(kernel));
        frame::Rect touched; for (auto n : run) touched = frame::unite(touched, frame::intersect(draws[n].plan.bounds, draws[n].scissor_bounds));
        touched = frame::intersect(touched, {0, 0, int32_t(res.width), int32_t(res.height)});
        if (touched.empty()) { stats.executed += run.size(); return; }
        struct Tile { uint32_t x, y, w, h; std::vector<size_t> commands; };
        std::vector<Tile> tiles;
        const int32_t edge = int32_t(draws[run.front()].tile);
        for (int32_t ty = touched.y0 / edge * edge; ty < touched.y1; ty += edge) for (int32_t tx = touched.x0 / edge * edge; tx < touched.x1; tx += edge) {
            Tile tile{uint32_t(tx), uint32_t(ty), uint32_t(std::min<int32_t>(edge, int32_t(res.width) - tx)), uint32_t(std::min<int32_t>(edge, int32_t(res.height) - ty)), {}};
            const frame::Rect rect{tx, ty, tx + int32_t(tile.w), ty + int32_t(tile.h)};
            for (auto n : run) if (!frame::intersect(frame::intersect(draws[n].plan.bounds, draws[n].scissor_bounds), rect).empty()) tile.commands.push_back(n);
            if (!tile.commands.empty()) tiles.push_back(std::move(tile));
        }
        // Each tile may need several sequential spans when its commands exceed the span budget.
        struct Pending { size_t tile; size_t first_command; };
        std::vector<Pending> pending; for (size_t t = 0; t < tiles.size(); ++t) pending.push_back({t, 0});
        while (!pending.empty()) {
            std::vector<std::vector<uint8_t>> spans; std::vector<Pending> next, in_flight;
            for (auto &p : pending) {
                if (spans.size() == FrameKernelProvider::rows) { next.push_back(p); continue; }
                auto &tile = tiles[p.tile];
                std::vector<std::vector<uint8_t>> programs; std::vector<const frame::LoweredProgram *> program_keys; std::vector<frame::Command> commands; std::vector<frame::Window> windows;
                std::vector<uint32_t> background; copy_rect(res.pixels.data(), res.width, {int32_t(tile.x), int32_t(tile.y), int32_t(tile.x + tile.w), int32_t(tile.y + tile.h)}, background);
                size_t consumed = p.first_command;
                for (; consumed < tile.commands.size(); ++consumed) {
                    auto &d = draws[tile.commands[consumed]];
                    frame::Command c; c.kind = 0;
                    size_t program_index = SIZE_MAX;
                    for (size_t k = 0; k < program_keys.size(); ++k) if (program_keys[k] == d.program.get()) program_index = k;
                    std::vector<std::vector<uint8_t>> trial_programs = programs; std::vector<frame::Window> trial_windows = windows;
                    if (program_index == SIZE_MAX) { program_index = programs.size(); trial_programs.push_back(d.kernel_entry); }
                    c.program = uint32_t(program_index); c.vertices = frame::pack_vertices(*d.program, d.inputs); c.vertex_count = d.record.vertex_count;
                    const auto clip = frame::intersect(frame::intersect(d.plan.bounds, d.scissor_bounds), {int32_t(tile.x), int32_t(tile.y), int32_t(tile.x + tile.w), int32_t(tile.y + tile.h)});
                    c.clip = {clip.x0 - int32_t(tile.x), clip.y0 - int32_t(tile.y), clip.x1 - int32_t(tile.x), clip.y1 - int32_t(tile.y)};
                    c.blend_mode = d.record.blend_mode; c.write_mask = d.record.write_mask; c.cull = d.record.cull; c.front_ccw = d.record.front_ccw;
                    c.blend_constant = {d.record.blend_constant[0], d.record.blend_constant[1], d.record.blend_constant[2], d.record.blend_constant[3]};
                    c.viewport = {d.record.viewport[0], d.record.viewport[1], d.record.viewport[2], d.record.viewport[3]}; c.depth = {d.record.viewport[4], d.record.viewport[5]};
                    c.constants = d.vertex_constants; c.constants.insert(c.constants.end(), d.pixel_constants.begin(), d.pixel_constants.end());
                    bool ok = true;
                    for (uint32_t t = 0; t < d.record.texture_count && ok; ++t) {
                        const auto &tref = refs[d.textures[t].resource]; frame::Window w;
                        auto r = frame::texel_window(*d.program, d.plan, clip, t, tref.record.width, tref.record.height, d.textures[t].filter != 0);
                        if (r.empty()) r = {0, 0, 1, 1};   // no triangle reaches this tile: a one-texel window keeps the contract
                        w.w = uint32_t(r.x1 - r.x0); w.h = uint32_t(r.y1 - r.y0); w.origin_x = uint32_t(r.x0); w.origin_y = uint32_t(r.y0);
                        w.texture_w = tref.record.width; w.texture_h = tref.record.height; w.format = tref.record.format;
                        if (!fetch_window(tref, r, w.pixels)) { ok = false; break; }
                        trial_windows.push_back(std::move(w)); c.textures[t].window = uint32_t(trial_windows.size() - 1); c.textures[t].point = d.textures[t].filter != 0;
                    }
                    if (!ok) throw std::runtime_error("frame batch texture window unavailable after planning");
                    std::vector<frame::Command> trial_commands = commands; trial_commands.push_back(c);
                    try {
                        auto span = frame::SpanBuilder::build(tile.x, tile.y, tile.w, tile.h, res.format, trial_programs, trial_commands, trial_windows, background.data());
                        programs = std::move(trial_programs); windows = std::move(trial_windows); commands = std::move(trial_commands);
                        if (program_index == programs.size() - 1 && program_keys.size() < programs.size()) program_keys.push_back(d.program.get());
                        (void)span;
                    } catch (const std::invalid_argument &) {
                        if (commands.empty()) throw std::runtime_error("frame batch span budget exceeded after fitting");
                        break;
                    }
                }
                spans.push_back(frame::SpanBuilder::build(tile.x, tile.y, tile.w, tile.h, res.format, programs, commands, windows, background.data()));
                in_flight.push_back({p.tile, consumed});
                if (consumed < tile.commands.size()) next.push_back({p.tile, consumed});
            }
            if (!spans.empty()) {
                std::vector<std::vector<uint8_t>> outputs; const auto began = std::chrono::steady_clock::now();
                kernel->execute(spans, outputs); ++stats.kernel_requests;
                stats.kernel_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
                for (size_t n = 0; n < outputs.size(); ++n) {
                    uint32_t status, executed, covered, escapes; std::memcpy(&status, outputs[n].data() + 4, 4); std::memcpy(&executed, outputs[n].data() + 8, 4); std::memcpy(&covered, outputs[n].data() + 12, 4); std::memcpy(&escapes, outputs[n].data() + 16, 4);
                    const auto &tile = tiles[in_flight[n].tile];
                    if (status != 0) {
                        // Keep the span for host-side diagnosis next to the failure packet.
                        char *failure = nullptr; size_t length = 0;
                        if (!_dupenv_s(&failure, &length, "NR_RENDER_FAILURE_PACKET") && failure && *failure) {
                            std::ofstream dump(std::filesystem::path(failure).replace_filename("frame-failed-span.bin"), std::ios::binary);
                            dump.write(reinterpret_cast<const char *>(spans[n].data()), std::streamsize(spans[n].size()));
                        }
                        std::free(failure);
                        throw std::runtime_error("frame kernel tile failed: status " + std::to_string(status) + " escapes " + std::to_string(escapes));
                    }
                    // Taps outside the packed windows clamp to the window edge; they are counted and reported, not fatal.
                    if (escapes) {
                        static bool dumped = false;
                        std::printf("{\"frame_kernel_escapes\":%u,\"tile_x\":%u,\"tile_y\":%u,\"commands\":%zu}\n", escapes, tile.x, tile.y, tile.commands.size()); std::fflush(stdout);
                        char *failure = nullptr; size_t length = 0;
                        if (!dumped && !_dupenv_s(&failure, &length, "NR_RENDER_FAILURE_PACKET") && failure && *failure) {
                            std::ofstream dump(std::filesystem::path(failure).replace_filename("frame-escaped-span-blob" + std::to_string(draws[run.front()].blob) + ".bin"), std::ios::binary);
                            dump.write(reinterpret_cast<const char *>(spans[n].data()), std::streamsize(spans[n].size())); dumped = true;
                        }
                        std::free(failure);
                    }
                    stats.escapes += escapes;
                    stats.covered += covered;
                    for (uint32_t y = 0; y < tile.h; ++y) std::memcpy(res.pixels.data() + size_t(tile.y + y) * res.width + tile.x, outputs[n].data() + NR_FRAME_OUTPUT_PIXELS + size_t(y) * tile.w * 4, size_t(tile.w) * 4);
                }
            }
            pending = std::move(next);
        }
        dirty[ref.record.key] = frame::unite(dirty[ref.record.key], touched);
        for (auto n : run) draws[n].done = true;
        stats.executed += run.size();
    }
};
}
