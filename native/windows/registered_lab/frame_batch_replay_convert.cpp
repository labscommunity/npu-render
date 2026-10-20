// Converts a recorded per-draw NPU request capture into frame batch packets.
// Per-draw mode wraps every recorded request in its own batch (target uploaded
// from the recorded background); chained mode joins consecutive draws on one
// target whose background equals the previous recorded result into one batch.
// The expected target hash after each batch is the recorded response hash.
#include "frame_batch_protocol.hpp"
#include "frame_batch_planner.hpp"
#include "frame_native_inventory.hpp"
#include "indexed_exchange_protocol.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
namespace ex = nr::registered_lab::indexed_exchange;
namespace fb = nr::registered_lab::frame_batch;
namespace {
std::string sha256_hex(const void *data, size_t bytes, std::array<unsigned char, 32> *raw = nullptr) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; ex::require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0);
    std::array<unsigned char, 32> digest{};
    const auto status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<void *>(data)), ULONG(bytes), digest.data(), ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0); ex::require(status >= 0);
    if (raw) *raw = digest;
    constexpr char hex[] = "0123456789abcdef"; std::string result; result.reserve(64);
    for (const auto byte : digest) { result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
    return result;
}
std::string field(const std::string &line, const char *name) {
    const auto key = std::string("\"") + name + "\":"; const auto at = line.find(key); if (at == std::string::npos) return {};
    auto start = at + key.size(); bool quoted = line[start] == '"'; if (quoted) ++start;
    auto end = quoted ? line.find('"', start) : line.find_first_of(",}", start);
    return line.substr(start, end - start);
}
struct Recorded { uint32_t ticket = 0; std::string file, response_sha; uint64_t pixel_bytes = 0; };
struct Texture { uint32_t width, height, format; const std::vector<nr::raster::PackedColor> *pixels; std::string sha; };
struct Batch {
    uint32_t width = 0, height = 0, format = 0; std::vector<nr::raster::PackedColor> background;
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> shaders; std::vector<std::string> shader_shas;
    std::vector<Texture> textures;
    std::vector<std::vector<uint8_t>> commands;   // CommandRecord + payload each
    std::vector<uint32_t> tickets; std::string expected_sha; uint64_t expected_bytes = 0, upload_bytes = 0;
};
uint32_t shader_index(Batch &b, uint32_t stage, const std::vector<uint32_t> &words) {
    const auto sha = sha256_hex(words.data(), words.size() * 4);
    for (size_t n = 0; n < b.shaders.size(); ++n) if (b.shader_shas[n] == sha && b.shaders[n].first == stage) return uint32_t(n);
    b.shaders.push_back({stage, words}); b.shader_shas.push_back(sha); return uint32_t(b.shaders.size() - 1);
}
uint32_t texture_index(Batch &b, const nr::raster::PackedTexture &t) {
    const auto sha = sha256_hex(t.pixels.data(), t.pixels.size() * sizeof(nr::raster::PackedColor)) + ":" + std::to_string(t.width) + "x" + std::to_string(t.height) + ":" + std::to_string(t.format);
    for (size_t n = 0; n < b.textures.size(); ++n) if (b.textures[n].sha == sha) return uint32_t(n);
    b.textures.push_back({t.width, t.height, t.format, &t.pixels, sha}); b.upload_bytes += t.pixels.size() * sizeof(nr::raster::PackedColor); return uint32_t(b.textures.size() - 1);
}
void add_command(Batch &b, const ex::Request &r) {
    const auto &h = r.header; fb::CommandRecord c; ex::Writer w;
    c.target = 0; c.legacy_abi = h.abi | (h.reserved << 16); c.legacy_flags = h.flags;
    if (ex::color_clear(h)) {
        c.kind = 1; for (unsigned k = 0; k < 4; ++k) c.clear_color[k] = r.clear_color[k];
        w.value(c); b.commands.push_back(w.finish()); return;
    }
    c.kind = 0; c.vertex_shader = shader_index(b, 1, r.vertex.bytecode); c.pixel_shader = shader_index(b, 2, r.pixel.bytecode);
    c.vertex_count = h.vertices; c.vertex_words = h.vertices * 64;
    uint32_t vs_words = 0, ps_words = 0;
    for (unsigned i = 0; i < 14; ++i) { vs_words += uint32_t(r.vertex_constants[i].size()) * 4; ps_words += uint32_t(r.pixel_constants[i].size()) * 4; }
    c.vertex_constant_words = vs_words; c.pixel_constant_words = ps_words;
    c.blend_mode = uint32_t(ex::blend_mode(h)); c.write_mask = (h.flags >> 8) & 15u; c.cull = (h.flags >> 5) & 3u; c.front_ccw = (h.flags & 128u) ? 1u : 0u;
    if (r.scissor) { for (unsigned k = 0; k < 4; ++k) c.scissor[k] = (*r.scissor)[k]; c.scissor_enabled = 1; }
    c.viewport[0] = r.viewport.x; c.viewport[1] = r.viewport.y; c.viewport[2] = r.viewport.width; c.viewport[3] = r.viewport.height; c.viewport[4] = r.viewport.min_depth; c.viewport[5] = r.viewport.max_depth;
    for (unsigned k = 0; k < 4; ++k) c.blend_constant[k] = r.blend_constant[k];
    std::vector<fb::TextureBinding> bindings;
    if (h.flags & 0x2000) {
        for (unsigned slot = 0; slot < 16; ++slot) if (r.pixel.textures[slot]) {
            ex::require(r.resources[slot].width && r.resources[slot].height);
            fb::TextureBinding t; t.resource = 1 + texture_index(b, r.resources[slot]);
            const auto &s = r.samplers[slot]; t.filter = s.filter == nr::shader::TextureFilter::point ? 1u : 0u; t.address_u = uint32_t(s.u); t.address_v = uint32_t(s.v);
            for (unsigned k = 0; k < 4; ++k) t.border[k] = s.border[k];
            bindings.push_back(t);
        }
    } else if (h.flags & 2) {
        unsigned used = 0; for (unsigned slot = 0; slot < 16; ++slot) used += r.pixel.textures[slot] ? 1u : 0u;
        ex::require(used == 1 && r.texture.width && r.texture.height);
        fb::TextureBinding t; t.resource = 1 + texture_index(b, r.texture); t.filter = (h.flags & 16) ? 1u : 0u; bindings.push_back(t);   // flag 16 selects point sampling
    }
    c.texture_count = uint32_t(bindings.size());
    w.value(c);
    for (const auto &t : bindings) w.value(t);
    for (const auto &v : r.inputs) for (const auto &reg : v) for (const auto lane : reg) w.value(lane);
    for (unsigned i = 0; i < 14; ++i) for (const auto &v : r.vertex_constants[i]) for (const auto lane : v) w.value(lane);
    for (unsigned i = 0; i < 14; ++i) for (const auto &v : r.pixel_constants[i]) for (const auto lane : v) w.value(lane);
    b.commands.push_back(w.finish());
}
// Mirrors the driver's decision: a draw joins a batch only when its shader pair is
// a compiled inventory program and the kernel can plan it; everything else stays
// on the recorded per-draw route.
bool batchable(const ex::Request &r, const nr::frame::NativeInventory &inventory, std::string &reason) {
    const auto &h = r.header;
    if (ex::color_clear(h)) return true;
    nr::shader::TextureSamplers samplers{};
    std::array<uint32_t, 4> tw{}, th{}; std::array<bool, 4> point{}; uint32_t count = 0;
    if (h.flags & 0x2000) {
        samplers = r.samplers;
        for (unsigned slot = 0; slot < 16; ++slot) if (r.pixel.textures[slot]) { if (count < 4) { tw[count] = r.resources[slot].width; th[count] = r.resources[slot].height; point[count] = r.samplers[slot].filter == nr::shader::TextureFilter::point; } ++count; }
    } else if (h.flags & 2) {
        for (auto &s : samplers) s.filter = (h.flags & 16) ? nr::shader::TextureFilter::point : nr::shader::TextureFilter::linear;
        tw[0] = r.texture.width; th[0] = r.texture.height; point[0] = (h.flags & 16) != 0; count = 1;
    }
    if (count > 4) { reason = "texture count"; return false; }
    auto lowered = nr::frame::lower(r.vertex, r.pixel, samplers);
    if (!lowered.eligible()) { reason = lowered.rejection; return false; }
    const auto *identity = inventory.find(lowered);
    if (!identity) { reason = "not a compiled inventory program"; return false; }
    if (lowered.texture_slots.size() != count) { reason = "texture count mismatch"; return false; }
    lowered.set_native(identity->host_index);
    std::vector<uint32_t> constants;
    for (unsigned i = 0; i < 14; ++i) for (const auto &v : r.vertex_constants[i]) constants.insert(constants.end(), v.begin(), v.end());
    for (unsigned i = 0; i < 14; ++i) for (const auto &v : r.pixel_constants[i]) constants.insert(constants.end(), v.begin(), v.end());
    const auto plan = nr::frame::plan_draw(lowered, nr::frame::pack_vertices(lowered, r.inputs), h.vertices, constants, {r.viewport.x, r.viewport.y, r.viewport.width, r.viewport.height}, tw, th, point);
    if (!plan.valid) { reason = plan.rejection; return false; }
    return true;
}
std::vector<uint8_t> encode(const Batch &b, uint32_t sequence) {
    ex::Writer w; ex::Header h; h.abi = fb::abi; h.kind = 1; h.device = 0; h.sequence = sequence; h.client_pid = 1; h.nonce[0] = sequence; h.nonce[1] = 0x6672616d65ull;
    h.width = b.width; h.height = b.height; h.format = b.format; h.flags = 0; h.vs_words = h.ps_words = h.vertices = 0;
    w.value(h);
    fb::BatchHeader bh; bh.shader_count = uint32_t(b.shaders.size()); bh.resource_count = uint32_t(1 + b.textures.size()); bh.upload_count = bh.resource_count; bh.command_count = uint32_t(b.commands.size());
    bh.batch_sequence = sequence; w.value(bh);
    for (size_t n = 0; n < b.shaders.size(); ++n) {
        fb::ShaderRecord s; s.stage = b.shaders[n].first; s.words = uint32_t(b.shaders[n].second.size());
        std::array<unsigned char, 32> raw{}; sha256_hex(b.shaders[n].second.data(), b.shaders[n].second.size() * 4, &raw); std::memcpy(s.sha256, raw.data(), 32);
        w.value(s); w.append(b.shaders[n].second.data(), b.shaders[n].second.size() * 4);
    }
    fb::ResourceRecord target; target.key = 1; target.version = sequence; target.width = b.width; target.height = b.height; target.format = b.format; w.value(target);
    for (size_t n = 0; n < b.textures.size(); ++n) { fb::ResourceRecord t; t.key = 2 + n; t.version = sequence; t.width = b.textures[n].width; t.height = b.textures[n].height; t.format = b.textures[n].format; w.value(t); }
    fb::UploadRecord up; up.resource = 0; up.width = b.width; up.height = b.height; w.value(up); w.append(b.background.data(), b.background.size() * sizeof(nr::raster::PackedColor));
    for (size_t n = 0; n < b.textures.size(); ++n) { fb::UploadRecord u; u.resource = uint32_t(1 + n); u.width = b.textures[n].width; u.height = b.textures[n].height; w.value(u); w.append(b.textures[n].pixels->data(), b.textures[n].pixels->size() * sizeof(nr::raster::PackedColor)); }
    for (const auto &c : b.commands) w.append(c.data(), c.size());
    auto bytes = w.finish(); h.body_bytes = uint32_t(bytes.size() - sizeof(h)); std::memcpy(bytes.data(), &h, sizeof(h));
    ex::require(fb::batch_header_valid(h));
    return bytes;
}
}
int wmain(int argc, wchar_t **argv) try {
    if (argc < 4) throw std::invalid_argument("usage: <capture dir> <output dir> <per-draw|chained> [max tickets]");
    const std::filesystem::path capture(argv[1]), output(argv[2]); const bool chained = !std::wcscmp(argv[3], L"chained");
    ex::require(chained || !std::wcscmp(argv[3], L"per-draw"));
    const uint32_t limit = argc > 4 ? uint32_t(std::wcstoul(argv[4], nullptr, 10)) : UINT32_MAX;
    std::filesystem::create_directories(output);
    std::map<uint32_t, Recorded> recorded;
    { std::ifstream index(capture / "index.jsonl"); std::string line;
      while (std::getline(index, line)) {
          if (line.find("\"request_recorded\":true") != std::string::npos) { Recorded r; r.ticket = uint32_t(std::stoul(field(line, "ticket"))); r.file = field(line, "file"); recorded[r.ticket] = r; }
          else if (line.find("\"provider_response_recorded\":true") != std::string::npos) { auto &r = recorded[uint32_t(std::stoul(field(line, "ticket")))]; r.response_sha = field(line, "pixels_sha256"); r.pixel_bytes = std::stoull(field(line, "pixel_bytes")); }
      } }
    nr::frame::NativeInventory inventory;
    std::vector<std::unique_ptr<ex::Request>> requests; std::vector<const Recorded *> rows; std::vector<std::string> legacy_reason;
    std::map<std::string, uint32_t> reasons;
    for (const auto &[ticket, r] : recorded) {
        if (ticket >= limit) break;
        if (r.file.empty() || r.response_sha.empty()) continue;
        std::ifstream f(capture / r.file, std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        requests.push_back(std::make_unique<ex::Request>(ex::decode(bytes))); rows.push_back(&r);
        std::string reason; if (!batchable(*requests.back(), inventory, reason)) ++reasons[reason];
        legacy_reason.push_back(reason);
    }
    std::ofstream list(output / "batches.jsonl"); uint32_t sequence = 0, legacy = 0; size_t n = 0;
    while (n < requests.size()) {
        if (!legacy_reason[n].empty()) {
            // Not batchable: the recorded per-draw packet replays as is.
            ++legacy;
            list << "{\"legacy\":true,\"file\":\"" << rows[n]->file << "\",\"reason\":\"" << legacy_reason[n] << "\",\"expected_sha256\":\"" << rows[n]->response_sha << "\",\"expected_bytes\":" << rows[n]->pixel_bytes << ",\"tickets\":[" << rows[n]->ticket << "]}\n";
            ++n; continue;
        }
        Batch b; const auto &first = *requests[n]; b.width = first.header.width; b.height = first.header.height; b.format = first.header.format;
        if (ex::color_clear(first.header)) b.background.assign(size_t(b.width) * b.height, nr::raster::PackedColor{}); else b.background = first.background;
        b.upload_bytes = b.background.size() * sizeof(nr::raster::PackedColor);
        std::string background_sha = sha256_hex(b.background.data(), b.background.size() * sizeof(nr::raster::PackedColor));
        size_t m = n;
        for (; m < requests.size(); ++m) {
            const auto &r = *requests[m];
            if (m > n) {
                if (!chained || !legacy_reason[m].empty() || r.header.width != b.width || r.header.height != b.height || r.header.format != b.format || b.commands.size() >= 256) break;
                if (ex::color_clear(r.header)) break;   // a clear starts a fresh chain
                if (sha256_hex(r.background.data(), r.background.size() * sizeof(nr::raster::PackedColor)) != rows[m - 1]->response_sha) break;
            }
            add_command(b, r);
            b.tickets.push_back(rows[m]->ticket); b.expected_sha = rows[m]->response_sha; b.expected_bytes = rows[m]->pixel_bytes;
            if (b.upload_bytes > 40u * 1024 * 1024) { ++m; break; }
        }
        ++sequence;
        const auto bytes = encode(b, sequence);
        char name[64]; std::snprintf(name, sizeof name, "batch-%06u.bin", sequence);
        std::ofstream(output / name, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        list << "{\"batch\":" << sequence << ",\"file\":\"" << name << "\",\"bytes\":" << bytes.size() << ",\"width\":" << b.width << ",\"height\":" << b.height << ",\"format\":" << b.format
             << ",\"commands\":" << b.commands.size() << ",\"textures\":" << b.textures.size() << ",\"background_sha256\":\"" << background_sha << "\",\"expected_sha256\":\"" << b.expected_sha
             << "\",\"expected_bytes\":" << b.expected_bytes << ",\"tickets\":[";
        for (size_t k = 0; k < b.tickets.size(); ++k) list << (k ? "," : "") << b.tickets[k];
        list << "]}\n";
        n = m;
    }
    std::printf("{\"frame_batch_convert\":true,\"requests\":%zu,\"batches\":%u,\"legacy\":%u,\"mode\":\"%s\",\"legacy_reasons\":{", requests.size(), sequence, legacy, chained ? "chained" : "per-draw");
    bool first_reason = true; for (const auto &[reason, count] : reasons) { std::printf("%s\"%s\":%u", first_reason ? "" : ",", reason.c_str(), count); first_reason = false; }
    std::printf("}}\n");
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
