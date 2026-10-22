// Renders one dumped frame kernel span on the host and reports, per draw
// command, the packed texture windows against the texel ranges its planned
// coordinates reach. CPU only; a diagnostic for escaped or failed spans.
#include "frame_kernel.hpp"
#include "frame_native_inventory.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace nr;
int main(int argc, char **argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: frame_span_debug <span.bin>\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> span((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (span.size() != frame::span_bytes) { std::fprintf(stderr, "span size %zu\n", span.size()); return 2; }
    // Kernel spans carry blob-local native indices; map them onto the host header's indices.
    if (argc >= 3) {
        const uint32_t blob = uint32_t(std::atoi(argv[2])); frame::NativeInventory inventory;
        uint32_t programs_offset, program_count; std::memcpy(&programs_offset, span.data() + 36, 4); std::memcpy(&program_count, span.data() + 40, 4);
        for (uint32_t n = 0; n < program_count; ++n) {
            uint32_t entry, native; std::memcpy(&entry, span.data() + programs_offset + 4 + n * 4, 4); std::memcpy(&native, span.data() + entry, 4);
            const uint32_t host = inventory.host_index(blob, native);
            std::printf("{\"program\":%u,\"blob_native\":%u,\"host_native\":%u}\n", n, native, host);
            std::memcpy(span.data() + entry, &host, 4);
        }
    }
    std::vector<uint8_t> output; const auto status = frame::render_reference(span, output);
    uint32_t executed, covered, escapes; std::memcpy(&executed, output.data() + 8, 4); std::memcpy(&covered, output.data() + 12, 4); std::memcpy(&escapes, output.data() + 16, 4);
    std::printf("{\"status\":%u,\"executed\":%u,\"covered\":%u,\"escapes\":%u}\n", status, executed, covered, escapes);
    frame::core::nr_span s{}; std::vector<uint8_t> scratch(frame::span_bytes);
    if (!frame::core::nr_span_load(&s, span.data(), scratch.data())) { std::printf("{\"span_load\":false}\n"); return 1; }
    for (uint32_t n = 0; n < s.command_count; ++n) {
        frame::core::nr_command cmd{}; if (!frame::core::nr_command_load(&s, n, &cmd)) { std::printf("{\"command\":%u,\"load\":false}\n", n); continue; }
        if (cmd.kind != 0) { std::printf("{\"command\":%u,\"kind\":\"fill\"}\n", n); continue; }
        frame::core::nr_program p{}; frame::core::nr_command_state st{};
        if (!frame::core::nr_program_load(&s, cmd.program, &p) || !frame::core::nr_command_state_load(&s, &cmd, &p, &st)) { std::printf("{\"command\":%u,\"program_or_state\":false}\n", n); continue; }
        frame::core::nr_shade sh{}; uint32_t esc = 0; sh.escapes = &esc;
        std::printf("{\"command\":%u,\"native\":%u,\"vertices\":%u,\"clip\":[%d,%d,%d,%d],\"textures\":[", n, p.native, cmd.vertex_count, cmd.clip[0], cmd.clip[1], cmd.clip[2], cmd.clip[3]);
        for (unsigned t = 0; t < p.texture_count; ++t) std::printf("%s{\"window\":[%u,%u,%u,%u],\"texture\":[%u,%u],\"point\":%u}", t ? "," : "", st.origin_x[t], st.origin_y[t], st.window_w[t], st.window_h[t], st.texture_w[t], st.texture_h[t], st.point[t]);
        std::printf("],\"projected\":[");
        for (uint32_t v = 0; v < cmd.vertex_count; ++v) {
            frame::core::nr_projected q{};
            const bool ok = frame::core::nr_vertex(&s, &cmd, &st, &p, v, &sh, &q);
            std::printf("%s{\"ok\":%d,\"x\":%.3f,\"y\":%.3f,\"inverse_w\":%.6f,\"varyings\":[", v ? "," : "", ok, q.x, q.y, q.inverse_w);
            for (uint32_t k = 0; k < p.varying_count; ++k) std::printf("%s[%.5f,%.5f,%.5f,%.5f]", k ? "," : "", NR_V4_LANE(q.varying[k], 0), NR_V4_LANE(q.varying[k], 1), NR_V4_LANE(q.varying[k], 2), NR_V4_LANE(q.varying[k], 3));
            std::printf("]}");
        }
        std::printf("]}\n");
    }
    return 0;
}
