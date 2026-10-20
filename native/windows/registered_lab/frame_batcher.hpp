// Frame batching for the native UMD. Admitted draws and clears are recorded
// into one abi-24 packet per flush instead of one request per draw. Every
// allocation the worker has seen stays resident there, keyed by the identity
// of its backing pixels and a content version. Targets upload only the region
// the CPU wrote since the worker last held them (usually nothing: the worker
// produced the pixels and the dirty rectangles were written back). Surfaces
// another process can write (shared or opened) are re-read before each batch
// and uploaded in full only when their pixels changed. After each flush only
// the dirty rectangles return and are written into VidMm, so the kernel copy
// of every target stays current at each flush point (present, flush, reads of
// pending regions, CPU writes to referenced resources).
#pragma once
#include "frame_batch_protocol.hpp"
#include "frame_batch_planner.hpp"
#include "frame_native_inventory.hpp"
#include "single_mip_filter.hpp"
#include <bcrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace frame_batch_detail {
namespace fb = nr::registered_lab::frame_batch;
inline std::array<uint8_t, 32> digest(const void *data, size_t bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0);
    std::array<uint8_t, 32> out{};
    const auto status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<void *>(data)), ULONG(bytes), out.data(), ULONG(out.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0); require(status >= 0);
    return out;
}
inline std::string short_hex(const std::array<uint8_t, 32> &d) {
    char text[9]; std::snprintf(text, sizeof text, "%02x%02x%02x%02x", d[0], d[1], d[2], d[3]); return text;
}
// Packs a surface's bytes as the worker expects: A8 expands to RGBA with zero color.
inline std::vector<uint8_t> packed(const std::vector<uint8_t> &raw, UINT format) {
    if (format != 65) return raw;
    std::vector<uint8_t> out(raw.size() * 4, 0);
    for (size_t n = 0; n < raw.size(); ++n) out[n * 4 + 3] = raw[n];
    return out;
}
// Crops a rectangle out of a full-surface readback (raw bytes).
inline std::vector<uint8_t> crop(const std::vector<uint8_t> &raw, UINT width, UINT texel, const nr::frame::Rect &r) {
    std::vector<uint8_t> out(size_t(r.x1 - r.x0) * (r.y1 - r.y0) * texel);
    for (int32_t y = r.y0; y < r.y1; ++y) std::memcpy(out.data() + size_t(y - r.y0) * (r.x1 - r.x0) * texel, raw.data() + (size_t(y) * width + r.x0) * texel, size_t(r.x1 - r.x0) * texel);
    return out;
}
}
// Set while a flush writes the worker's results back into VidMm: those uploads
// synchronize the kernel copy and must not mark the target dirty again.
bool frame_batch_writeback = false;
bool frame_batch_writeback_active() { return frame_batch_writeback; }
struct FrameBatcher {
    using Rect = nr::frame::Rect;
    Device &d;
    nr::frame::NativeInventory inventory;
    bool flushing = false;
    uint64_t flushes = 0, draws = 0, clears = 0, kernel_requests = 0, writeback_bytes = 0, upload_bytes = 0, snapshot_bytes = 0;
    double kernel_ms = 0, host_ms = 0;
    struct Lowered { std::shared_ptr<nr::frame::LoweredProgram> program; const nr::frame::NativeIdentity *identity = nullptr; };
    std::map<std::string, Lowered> lowered;
    std::map<const nr::shader::Program *, std::array<uint8_t, 32>> digests;
    struct ShaderEntry { std::shared_ptr<const nr::shader::Program> program; uint32_t stage; };
    struct ResourceEntry {
        std::shared_ptr<Allocation> allocation; bool is_volatile = false, target = false; uint64_t version = 0;
        std::vector<uint8_t> full;          // full upload (packed) when non-empty
        Rect window{}; std::vector<uint8_t> partial;   // partial upload (packed) of `window` when non-empty
        std::vector<Rect> pending;          // bounds of this batch's commands on the target
    };
    struct CommandEntry { frame_batch_detail::fb::CommandRecord record; std::vector<frame_batch_detail::fb::TextureBinding> bindings; std::vector<uint32_t> vertices, constants; };
    std::vector<ShaderEntry> shaders; std::vector<ResourceEntry> resources; std::vector<CommandEntry> commands;
    size_t bytes = 0;
    explicit FrameBatcher(Device &device) : d(device) {}
    bool referenced(const Allocation &a) const { for (const auto &r : resources) if (r.allocation.get() == &a) return true; return false; }
    bool pending(const Allocation &a) const { for (const auto &r : resources) if (r.allocation.get() == &a && r.target) return true; return false; }
    // True when this batch still has to write pixels of `a` inside `box` (any pixels when box is null).
    bool pending_in(const Allocation &a, const D3D10_DDI_BOX *box) const {
        for (const auto &r : resources) {
            if (r.allocation.get() != &a || !r.target) continue;
            if (!box) return true;
            const Rect read{box->left, box->top, box->right, box->bottom};
            for (const auto &pending_rect : r.pending) if (!nr::frame::intersect(pending_rect, read).empty()) return true;
        }
        return false;
    }
    const std::array<uint8_t, 32> &program_digest(const std::shared_ptr<const nr::shader::Program> &program) {
        auto found = digests.find(program.get());
        if (found == digests.end()) found = digests.emplace(program.get(), frame_batch_detail::digest(program->bytecode.data(), program->bytecode.size() * 4)).first;
        return found->second;
    }
    uint32_t shader_index(const std::shared_ptr<const nr::shader::Program> &program, uint32_t stage) {
        for (size_t n = 0; n < shaders.size(); ++n) if (shaders[n].program == program) return uint32_t(n);
        program_digest(program);
        shaders.push_back({program, stage}); bytes += sizeof(frame_batch_detail::fb::ShaderRecord) + program->bytecode.size() * 4;
        return uint32_t(shaders.size() - 1);
    }
    // Registers an allocation for this batch and decides what the worker still needs of its pixels.
    uint32_t resource_index(const std::shared_ptr<Allocation> &a, bool as_target) {
        for (size_t n = 0; n < resources.size(); ++n) if (resources[n].allocation == a) {
            if (as_target) resources[n].target = true;
            return uint32_t(n);
        }
        ResourceEntry r; r.allocation = a; r.is_volatile = a->runtime_resource != nullptr; r.target = as_target;
        const auto &s = a->surface;
        if (!as_target && r.is_volatile) {
            // Another process may write a shared surface between frames: compare the current
            // pixels with the snapshot the worker holds and upload them again only when they
            // differ. The compositor reads each surface once per frame, so one comparison per
            // present is enough.
            const uint64_t frame = counters.present_requests + 1;
            if (a->snapshot_frame != frame || !a->worker_version) {
                auto snapshot = read_pixels(*a); snapshot_bytes += snapshot.size(); a->snapshot_frame = frame;
                if (!a->worker_version || a->shadow.size() != snapshot.size() || std::memcmp(a->shadow.data(), snapshot.data(), snapshot.size()) != 0) {
                    a->content_version = next_content_version(); a->shadow = snapshot;
                    r.full = frame_batch_detail::packed(snapshot, s.format);
                }
            }
            a->clear_dirty();
        } else if (a->worker_version != a->content_version) {
            const Rect dirty{a->dirty_x0, a->dirty_y0, a->dirty_x1, a->dirty_y1};
            if (a->worker_version && !a->dirty_full && !dirty.empty()) {
                // The worker holds the previous content; only the CPU-written region travels.
                r.window = nr::frame::intersect(dirty, {0, 0, int32_t(s.width), int32_t(s.height)});
                r.partial = frame_batch_detail::packed(frame_batch_detail::crop(read_pixels(*a), s.width, texel_bytes(s.format), r.window), s.format);
            } else r.full = frame_batch_detail::packed(read_pixels(*a), s.format);
        }
        r.version = a->content_version;
        bytes += sizeof(frame_batch_detail::fb::ResourceRecord) + (r.full.empty() ? 0 : r.full.size() + sizeof(frame_batch_detail::fb::UploadRecord)) +
                 (r.partial.empty() ? 0 : r.partial.size() + sizeof(frame_batch_detail::fb::UploadRecord));
        resources.push_back(std::move(r));
        return uint32_t(resources.size() - 1);
    }
    struct TextureUse { UINT slot; std::shared_ptr<Allocation> allocation; bool point; };
    // Records one draw. Returns false (with a reason) when the draw must take the per-draw route.
    bool record_draw(const std::shared_ptr<Allocation> &target, const std::vector<nr::shader::VertexRegisters> &inputs,
                     const std::shared_ptr<const nr::shader::Program> &vs, const std::shared_ptr<const nr::shader::Program> &ps,
                     const nr::shader::VertexConstants &vc, const nr::shader::VertexConstants &pc, nr::raster::BlendMode blend_mode, uint32_t write_mask,
                     nr::raster::CullMode cull, bool front_ccw, const std::array<int32_t, 4> &clip, bool scissored, const nr::raster::Viewport &viewport,
                     const nr::raster::Color &blend_constant, std::string &reason) {
        namespace fb = frame_batch_detail::fb;
        const std::string ps_id = " ps=" + frame_batch_detail::short_hex(program_digest(ps));
        if (inputs.size() > 4095) { reason = "vertex count" + ps_id; return false; }
        // Textures: 2D color surfaces sampled with clamp addressing, in SM4 slot order.
        std::vector<TextureUse> uses; nr::shader::TextureSamplers samplers{}; std::string key = std::to_string(uintptr_t(vs.get())) + ":" + std::to_string(uintptr_t(ps.get()));
        std::string texture_shapes;
        for (UINT slot = 0; slot < 16; ++slot) {
            if (ps->samplers[slot]) {
                const auto &samp = d.samplers[2][slot];
                if (!samp.bound || !single_mip_filter_supported(UINT(samp.desc.Filter)) || samp.desc.AddressU != D3D10_DDI_TEXTURE_ADDRESS_CLAMP || samp.desc.AddressV != D3D10_DDI_TEXTURE_ADDRESS_CLAMP ||
                    samp.desc.AddressW != D3D10_DDI_TEXTURE_ADDRESS_CLAMP || samp.desc.MipLODBias != 0 || samp.desc.MinLOD != 0 || samp.desc.MaxLOD < 0) {
                    char detail[160];
                    std::snprintf(detail, sizeof detail, "sampler state: slot %u bound %d filter %u address %u/%u/%u lod %g/%g/%g", slot, samp.bound ? 1 : 0, UINT(samp.desc.Filter),
                                  UINT(samp.desc.AddressU), UINT(samp.desc.AddressV), UINT(samp.desc.AddressW), double(samp.desc.MipLODBias), double(samp.desc.MinLOD), double(samp.desc.MaxLOD));
                    reason = detail + ps_id; return false;
                }
                samplers[slot].filter = single_mip_linear_filter(UINT(samp.desc.Filter)) ? nr::shader::TextureFilter::linear : nr::shader::TextureFilter::point;
                key += samplers[slot].filter == nr::shader::TextureFilter::point ? "p" : "l";
            }
            if (!ps->textures[slot]) continue;
            const auto &view = d.shader_sources[2][slot];
            if (view.buffer || !view.allocation || ps->texture_dimensions[slot] != 3 || ps->texture_uint[slot]) { reason = "texture view" + ps_id; return false; }
            const auto &ss = view.allocation->surface;
            texture_shapes += " t" + std::to_string(slot) + "=" + std::to_string(ss.width) + "x" + std::to_string(ss.height) + (view.allocation->runtime_resource ? "s" : "p");
            if (!(ss.format == 28 || ss.format == 87 || ss.format == 65) || ss.width > 8192 || ss.height > 8192 || uint64_t(ss.width) * ss.height > 8388608) { reason = "texture shape" + ps_id + texture_shapes; return false; }
            if (view.allocation == target || view.allocation->handle == target->handle) { reason = "target sampled" + ps_id; return false; }
            uses.push_back({slot, view.allocation, false});
        }
        auto found = lowered.find(key);
        if (found == lowered.end()) {
            Lowered l; l.program = std::make_shared<nr::frame::LoweredProgram>(nr::frame::lower(*vs, *ps, samplers));
            if (l.program->eligible()) { l.identity = inventory.find(*l.program); if (l.identity) l.program->set_native(l.identity->host_index); }
            found = lowered.emplace(key, std::move(l)).first;
        }
        const auto &lw = found->second;
        if (!lw.program->eligible()) { reason = "lowering: " + lw.program->rejection + ps_id; return false; }
        if (!lw.identity) { reason = "not a compiled inventory program" + ps_id + " vs=" + frame_batch_detail::short_hex(program_digest(vs)); return false; }
        const auto &program = *lw.program;
        if (program.texture_slots.size() != uses.size()) { reason = "texture count" + ps_id; return false; }
        std::array<uint32_t, 4> tw{}, th{}; std::array<bool, 4> point{}; std::vector<const TextureUse *> ordered;
        for (size_t t = 0; t < program.texture_slots.size(); ++t) {
            const TextureUse *use = nullptr; for (const auto &u : uses) if (u.slot == program.texture_slots[t]) use = &u;
            if (!use) { reason = "texture slot" + ps_id; return false; }
            tw[t] = use->allocation->surface.width; th[t] = use->allocation->surface.height; point[t] = samplers[program.sampler_slots[t]].filter == nr::shader::TextureFilter::point; ordered.push_back(use);
        }
        std::vector<uint32_t> constants, vertices; vertices.reserve(inputs.size() * 64);
        for (unsigned i = 0; i < 14; ++i) for (const auto &v : vc[i]) constants.insert(constants.end(), v.begin(), v.end());
        const uint32_t vs_words = uint32_t(constants.size());
        for (unsigned i = 0; i < 14; ++i) for (const auto &v : pc[i]) constants.insert(constants.end(), v.begin(), v.end());
        if (vs_words != program.vs_constants * 4 || constants.size() - vs_words != program.ps_constants * 4) { reason = "constant words" + ps_id; return false; }
        for (const auto &v : inputs) for (const auto &reg : v) vertices.insert(vertices.end(), reg.begin(), reg.end());
        const auto plan = nr::frame::plan_draw(program, nr::frame::pack_vertices(program, inputs), uint32_t(inputs.size()), constants, {viewport.x, viewport.y, viewport.width, viewport.height}, tw, th, point);
        if (!plan.valid) { reason = "plan: " + plan.rejection + ps_id; return false; }
        const auto &s = target->surface;
        const Rect bounds = nr::frame::intersect(nr::frame::intersect(plan.bounds, {clip[0], clip[1], clip[2], clip[3]}), {0, 0, int32_t(s.width), int32_t(s.height)});
        if (!nr::frame::fitting_tile(program, plan, bounds, program.entry.size(), uint32_t(inputs.size()) * 64, uint32_t(constants.size()), tw, th, point)) {
            std::string windows;
            for (size_t t = 0; t < program.texture_slots.size(); ++t) {
                const Rect w = plan.whole[t] ? Rect{0, 0, int32_t(tw[t]), int32_t(th[t])} : plan.texels[t];
                windows += " w" + std::to_string(t) + "=" + std::to_string(w.x1 - w.x0) + "x" + std::to_string(w.y1 - w.y0) + (plan.whole[t] ? "whole" : "");
            }
            reason = "span budget" + ps_id + " bounds=" + std::to_string(bounds.x1 - bounds.x0) + "x" + std::to_string(bounds.y1 - bounds.y0) + " vertices=" + std::to_string(inputs.size()) + texture_shapes + windows;
            return false;
        }
        // A texture rendered earlier in this batch must be flushed first; so must an oversized batch.
        bool hazard = false; for (const auto *use : ordered) hazard = hazard || pending(*use->allocation);
        if (hazard) flush("hazard"); else if (commands.size() >= 1000 || bytes > 40u * 1024 * 1024) flush("limit");
        CommandEntry c; auto &r = c.record;
        r.kind = 0; r.target = resource_index(target, true);
        r.vertex_shader = shader_index(vs, 1); r.pixel_shader = shader_index(ps, 2);
        r.vertex_count = uint32_t(inputs.size()); r.vertex_words = r.vertex_count * 64; r.vertex_constant_words = vs_words; r.pixel_constant_words = uint32_t(constants.size()) - vs_words;
        r.blend_mode = uint32_t(blend_mode); r.write_mask = write_mask; r.cull = uint32_t(cull); r.front_ccw = front_ccw ? 1u : 0u;
        if (scissored) { for (unsigned k = 0; k < 4; ++k) r.scissor[k] = clip[k]; r.scissor_enabled = 1; }
        r.viewport[0] = viewport.x; r.viewport[1] = viewport.y; r.viewport[2] = viewport.width; r.viewport[3] = viewport.height; r.viewport[4] = viewport.min_depth; r.viewport[5] = viewport.max_depth;
        for (unsigned k = 0; k < 4; ++k) r.blend_constant[k] = blend_constant[k];
        r.texture_count = uint32_t(ordered.size()); r.legacy_abi = 0; r.legacy_flags = 0;
        for (size_t t = 0; t < ordered.size(); ++t) {
            fb::TextureBinding b; b.resource = resource_index(ordered[t]->allocation, false); b.filter = point[t] ? 1u : 0u;
            c.bindings.push_back(b);
        }
        extend_pending(resources[r.target], bounds);
        c.vertices = std::move(vertices); c.constants = std::move(constants);
        bytes += sizeof(fb::CommandRecord) + c.bindings.size() * sizeof(fb::TextureBinding) + c.vertices.size() * 4 + c.constants.size() * 4;
        commands.push_back(std::move(c)); ++draws;
        return true;
    }
    static void extend_pending(ResourceEntry &entry, const Rect &r) { if (!r.empty()) entry.pending.push_back(r); }
    void record_clear(const std::shared_ptr<Allocation> &target, const nr::raster::Color &color) {
        if (commands.size() >= 1000 || bytes > 40u * 1024 * 1024) flush("limit");
        CommandEntry c; c.record.kind = 1; c.record.target = resource_index(target, true);
        for (unsigned k = 0; k < 4; ++k) c.record.clear_color[k] = color[k];
        extend_pending(resources[c.record.target], {0, 0, int32_t(target->surface.width), int32_t(target->surface.height)});
        bytes += sizeof(frame_batch_detail::fb::CommandRecord); commands.push_back(std::move(c)); ++clears;
    }
    std::vector<uint8_t> encode(uint64_t sequence) const {
        namespace fb = frame_batch_detail::fb;
        nr::registered_lab::indexed_exchange::Writer w;
        fb::BatchHeader bh; bh.shader_count = uint32_t(shaders.size()); bh.resource_count = uint32_t(resources.size()); bh.command_count = uint32_t(commands.size()); bh.batch_sequence = sequence;
        uint32_t uploads = 0; for (const auto &r : resources) uploads += (r.full.empty() ? 0u : 1u) + (r.partial.empty() ? 0u : 1u);
        bh.upload_count = uploads; w.value(bh);
        for (const auto &s : shaders) { fb::ShaderRecord rec; rec.stage = s.stage; rec.words = uint32_t(s.program->bytecode.size()); std::memcpy(rec.sha256, digests.at(s.program.get()).data(), 32); w.value(rec); w.append(s.program->bytecode.data(), s.program->bytecode.size() * 4); }
        for (const auto &r : resources) { fb::ResourceRecord rec; rec.key = r.allocation->id; rec.version = r.version; rec.width = r.allocation->surface.width; rec.height = r.allocation->surface.height; rec.format = r.allocation->surface.format == 65 ? 28u : r.allocation->surface.format; rec.flags = r.is_volatile ? fb::volatile_resource : 0u; w.value(rec); }
        for (size_t n = 0; n < resources.size(); ++n) {
            const auto &r = resources[n];
            if (!r.full.empty()) { fb::UploadRecord up; up.resource = uint32_t(n); up.width = r.allocation->surface.width; up.height = r.allocation->surface.height; w.value(up); w.append(r.full.data(), r.full.size()); }
            if (!r.partial.empty()) { fb::UploadRecord up; up.resource = uint32_t(n); up.x = uint32_t(r.window.x0); up.y = uint32_t(r.window.y0); up.width = uint32_t(r.window.x1 - r.window.x0); up.height = uint32_t(r.window.y1 - r.window.y0); w.value(up); w.append(r.partial.data(), r.partial.size()); }
        }
        for (const auto &c : commands) { w.value(c.record); for (const auto &b : c.bindings) w.value(b); if (!c.vertices.empty()) w.append(c.vertices.data(), c.vertices.size() * 4); if (!c.constants.empty()) w.append(c.constants.data(), c.constants.size() * 4); }
        return w.finish();
    }
    // Sends the batch, resends with full uploads for anything the worker reports missing, writes dirty rectangles back.
    void flush(const char *reason) {
        namespace fb = frame_batch_detail::fb; namespace ex = nr::registered_lab::indexed_exchange;
        if (commands.empty() || flushing) return;
        flushing = true;
        struct Reset { bool &flag; ~Reset() { flag = false; } } reset{flushing};
        const auto started = GetTickCount64();
        if (!d.indexed_exchange) d.indexed_exchange = std::make_unique<IndexedNpuExchange>();
        const uint64_t sequence = next_content_version();
        std::vector<uint8_t> reply; uint32_t resent = 0;
        for (unsigned attempt = 0;; ++attempt) {
            ex::Header h; h.width = 0; h.height = 0; h.format = 0; h.flags = 0;
            reply = d.indexed_exchange->exchange_batch(h, encode(sequence));
            ex::Reader reader(reply); (void)reader.value<ex::Header>(); const auto response = reader.value<fb::ResponseHeader>();
            require(response.magic == fb::response_magic && response.version == 1 && response.batch_sequence == sequence);
            if (!response.missing_count) break;
            require(attempt < 2);
            for (uint32_t n = 0; n < response.missing_count; ++n) {
                const auto m = reader.value<fb::MissingRecord>(); bool found = false;
                for (auto &r : resources) if (r.allocation->id == m.key) {
                    found = true;
                    if (r.full.empty()) {
                        const bool shared_texture = r.is_volatile && !r.target && r.allocation->shadow.size() == size_t(r.allocation->surface.width) * r.allocation->surface.height * texel_bytes(r.allocation->surface.format);
                        r.full = frame_batch_detail::packed(shared_texture ? r.allocation->shadow : read_pixels(*r.allocation), r.allocation->surface.format);
                        r.partial.clear(); r.version = r.allocation->content_version; bytes += r.full.size(); ++resent;
                    }
                }
                require(found);
            }
            lab_trace("{\"event\":\"frame_batch_missing\",\"count\":%u,\"attempt\":%u}\n", response.missing_count, attempt + 1);
        }
        ex::Reader reader(reply); (void)reader.value<ex::Header>(); const auto response = reader.value<fb::ResponseHeader>();
        {
            struct Writeback { Writeback() { frame_batch_writeback = true; } ~Writeback() { frame_batch_writeback = false; } } writeback;
            for (uint32_t n = 0; n < response.target_count; ++n) {
                const auto t = reader.value<fb::TargetRecord>(); ResourceEntry *entry = nullptr;
                for (auto &r : resources) if (r.allocation->id == t.key) entry = &r;
                require(entry && t.width && t.height && t.x + t.width <= entry->allocation->surface.width && t.y + t.height <= entry->allocation->surface.height);
                const auto pixels = reader.vector<uint32_t>(size_t(t.width) * t.height);
                const D3D10_DDI_BOX box{LONG(t.x), LONG(t.y), 0, LONG(t.x + t.width), LONG(t.y + t.height), 1};
                upload(*entry->allocation, &box, pixels.data(), t.width * 4);
                writeback_bytes += pixels.size() * 4;
            }
        }
        require(reader.done());
        uint32_t uploads = 0; size_t uploaded = 0;
        for (auto &r : resources) {
            auto &a = *r.allocation;
            if (!r.full.empty() || !r.partial.empty()) { ++uploads; uploaded += r.full.size() + r.partial.size(); }
            if (r.target) { a.content_version = sequence; a.worker_version = sequence; a.clear_dirty(); }
            else { a.worker_version = r.version; if (!r.is_volatile) a.clear_dirty(); }
        }
        ++flushes; kernel_requests += response.provider_requests; kernel_ms += response.kernel_ms_x1000 / 1000.0; host_ms += response.host_ms_x1000 / 1000.0; upload_bytes += bytes;
        // The worker executed the batch on the NPU through the local indexed pipe; the
        // probe-visible device string matches the per-draw indexed route it replaces.
        strncpy_s(counters.last_execution_device, "NPU (local indexed)", _TRUNCATE);
        lab_trace("{\"event\":\"frame_batch_flushed\",\"reason\":\"%s\",\"sequence\":%llu,\"commands\":%zu,\"resources\":%zu,\"uploads\":%u,\"upload_bytes\":%zu,\"snapshot_bytes\":%llu,\"resent\":%u,\"bytes\":%zu,\"targets\":%u,\"writeback_bytes\":%llu,\"kernel_requests\":%llu,\"kernel_ms\":%.3f,\"worker_ms\":%.3f,\"wall_ms\":%llu,\"execution_device\":\"NPU\"}\n",
            reason, (unsigned long long)sequence, commands.size(), resources.size(), uploads, uploaded, (unsigned long long)snapshot_bytes, resent, bytes, response.target_count, (unsigned long long)writeback_bytes, (unsigned long long)response.provider_requests,
            response.kernel_ms_x1000 / 1000.0, response.host_ms_x1000 / 1000.0, (unsigned long long)(GetTickCount64() - started));
        writeback_bytes = 0; snapshot_bytes = 0;
        shaders.clear(); resources.clear(); commands.clear(); bytes = 0;
    }
};
// Flush hooks used by the DDI entry points (declared ahead of the batcher in driver.cpp).
std::vector<Device *> frame_devices;
void flush_frame_batches(Device &d, const char *reason) { if (d.batcher) d.batcher->flush(reason); }
void frame_batches_before_write(Allocation &a) { for (auto *dev : frame_devices) if (dev->batcher && !dev->batcher->flushing && dev->batcher->referenced(a)) dev->batcher->flush("write"); }
void frame_batches_before_read(Allocation &a, const D3D10_DDI_BOX *box) { for (auto *dev : frame_devices) if (dev->batcher && !dev->batcher->flushing && dev->batcher->pending_in(a, box)) dev->batcher->flush(box ? "read_region" : "read"); }
void frame_batches_before_read(Allocation &a) { frame_batches_before_read(a, nullptr); }
void frame_batch_clear(Device &d, const std::shared_ptr<Allocation> &target, const nr::raster::Color &color) {
    if (!d.batcher) d.batcher = std::make_shared<FrameBatcher>(d);
    d.batcher->record_clear(target, color);
}
