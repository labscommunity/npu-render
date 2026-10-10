// Offline replay of captured uniform-color triangle draws. No display control.
#include "capture_reader.hpp"
#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include "uniform_draw.hpp"
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <cstdio>
namespace {
nr::shader::VertexConstants constants(const nr::capture::Snapshot &s, unsigned stage, const nr::shader::Program &p) {
    nr::shader::VertexConstants result;
    for (unsigned i = 0; i < result.size(); ++i) if (p.constants[i]) {
        result[i] = s.values<nr::shader::VertexBits>("constants-" + std::to_string(stage) + "-" + std::to_string(i));
        nr::capture::check(result[i].size() >= p.constants[i]); result[i].resize(p.constants[i]);
    }
    return result;
}
std::vector<nr::shader::VertexRegisters> fetch(const nr::capture::Snapshot &s, uint32_t count, uint32_t first, int32_t base) {
    const auto id = s.values<uint32_t>("indices-description"), elements = s.values<uint32_t>("layout");
    nr::capture::check(id.size() == 4 && ((id[1] == 2 && id[3] == 57) || (id[1] == 4 && id[3] == 42)) && elements.size() % 6 == 0);
    const auto &ib = s.bytes("indices");
    nr::capture::check(id[0] == ib.size() && uint64_t(id[2]) + (uint64_t(first) + count) * id[1] <= ib.size());
    std::vector<uint32_t> indices(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t index = 0; std::memcpy(&index, ib.data() + id[2] + size_t(first + i) * id[1], id[1]);
        const int64_t adjusted = int64_t(index) + base; nr::capture::check(adjusted >= 0 && adjusted <= UINT32_MAX); indices[i] = uint32_t(adjusted);
    }
    std::vector<nr::shader::VertexRegisters> result(count);
    uint32_t registers = 0;
    for (size_t e = 0; e < elements.size(); e += 6) {
        const auto slot = elements[e], offset = elements[e + 1], format = elements[e + 2], reg = elements[e + 5];
        nr::capture::check(slot < 16 && reg < 16 && !elements[e + 3] && !elements[e + 4] && !(registers & (1u << reg)));
        registers |= 1u << reg;
        const auto name = "vertices-" + std::to_string(slot);
        const auto description = s.values<uint32_t>(name + "-description"); const auto &vb = s.bytes(name);
        nr::capture::check(description.size() == 4 && description[0] == vb.size() && description[1] <= 2048);
        // Captured desktop layout formats: float1/2/3/4 and RGBA8 UNORM.
        const auto lanes = format == 2 ? 4u : (format == 6 ? 3u : (format == 16 ? 2u : (format == 41 ? 1u : 0u)));
        nr::capture::check(lanes || format == 28); const auto bytes = format == 28 ? 4u : lanes * 4;
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t begin = uint64_t(description[2]) + offset + uint64_t(indices[i]) * description[1];
            nr::capture::check(begin <= vb.size() && bytes <= vb.size() - begin);
            auto &v = result[i][reg]; v = {0, 0, 0, 0x3f800000};
            if (format == 28) for (unsigned c = 0; c < 4; ++c) v[c] = nr::shader::as_bits(float(vb[size_t(begin) + c]) / 255);
            else std::memcpy(v.data(), vb.data() + size_t(begin), bytes);
        }
    }
    return result;
}
}
int main(int argc, char **argv) try {
    using namespace nr::shader;
    using namespace nr::raster;
    if ((argc != 7 && argc != 11) || std::strcmp(argv[1], "--capture") || std::strcmp(argv[3], "--backend") || std::strcmp(argv[5], "--output"))
        throw std::invalid_argument("expected --capture <NPDRAW01 file> --backend <CPU|NPU> --output <new pixel file>");
    uint32_t repeats = 1, batch = 4096;
    if (argc == 11) {
        nr::capture::check(!std::strcmp(argv[7], "--repeat") && !std::strcmp(argv[9], "--batch"));
        char *end = nullptr; const auto r = std::strtoul(argv[8], &end, 10);
        nr::capture::check(end != argv[8] && !*end && r && r <= 5); repeats = uint32_t(r);
        const auto b = std::strtoul(argv[10], &end, 10);
        nr::capture::check(end != argv[10] && !*end && b && b <= 4096); batch = uint32_t(b);
    }
    const std::string backend = argv[4]; nr::capture::check(backend == "CPU" || backend == "NPU");
    std::ifstream existing(argv[6], std::ios::binary); nr::capture::check(!existing); existing.close();
    const auto start = std::chrono::steady_clock::now();
    const nr::capture::Snapshot snapshot(argv[2]);
    const auto args = snapshot.values<uint32_t>("arguments");
    nr::capture::check(args.size() == 9 && args[0] && args[0] <= 65536 && !(args[0] % 3) && args[3] == 4 && args[4] == 1 && args[7] == UINT32_MAX);
    const auto view = snapshot.values<float>("viewport");
    const auto surface = snapshot.values<uint32_t>("target-description");
    nr::capture::check(view.size() == 6 && surface.size() == 10 && surface[0] == 0x4e52574b && surface[1] == 1 && surface[2] == 40 && !surface[7]);
    const uint32_t width = surface[3], height = surface[4], format = surface[5];
    nr::capture::check(width && width <= 8192 && height && height <= 8192 && (format == 28 || format == 87) &&
          view[0] == 0 && view[1] == 0 && view[2] == float(width) && view[3] == float(height));
    const auto blend = snapshot.values<uint32_t>("blend"), raster = snapshot.values<uint32_t>("raster"), depth = snapshot.values<uint32_t>("depth");
    nr::capture::check(blend.size() == 17 && !blend[0] && depth.size() == 15 && !depth[0] && !depth[3]);
    for (size_t i = 2; i < 9; ++i) nr::capture::check(!blend[i]);
    const bool blending = blend[1] != 0;
    if (blending) nr::capture::check(blend[9] == 2 && blend[10] == 6 && blend[11] == 1 && blend[12] == 2 && blend[13] == 6 && blend[14] == 1);
    nr::capture::check(raster.size() == 10 && raster[0] == 3 && raster[1] == 1 && !raster[3] && !raster[4] && !raster[5] && !raster[8] && !raster[9]);
    if (raster[7]) {
        const auto scissor = snapshot.values<int32_t>("scissors");
        nr::capture::check(scissor.size() == 4 && scissor[0] == 0 && scissor[1] == 0 && scissor[2] == int32_t(width) && scissor[3] == int32_t(height));
    }
    auto pixels = snapshot.values<PackedColor>("target"); nr::capture::check(pixels.size() == size_t(width) * height);
    const auto vs_words = snapshot.values<uint32_t>("vertex-shader"), ps_words = snapshot.values<uint32_t>("pixel-shader");
    const auto vs = parse_vertex(vs_words.data(), vs_words.size()), ps = parse_pixel(ps_words.data(), ps_words.size());
    nr::capture::check(vs.position_output < 16 && ps.outputs[0] == 15);
    for (size_t i = 1; i < ps.outputs.size(); ++i) nr::capture::check(!ps.outputs[i]);
    for (const auto used : ps.textures) nr::capture::check(!used);
    // Uniform specialization is valid only when PS does not read position.
    for (const auto &instruction : ps.instructions) for (unsigned i = 0; i < instruction.source_count; ++i)
        nr::capture::check(instruction.sources[i].kind != Register::input || instruction.sources[i].index != ps.position_input);
    int32_t base; std::memcpy(&base, &args[2], 4);
    const auto inputs = fetch(snapshot, args[0], args[1], base);
    const auto cb = constants(snapshot, 0, vs);
    const auto pixel_cb = constants(snapshot, 2, ps);
    const auto compile_start = std::chrono::steady_clock::now();
    UniformDrawExecutor executor(vs, ps, args[0], width, height, backend, blending, format == 87, uint8_t(blend[15] & 255), batch);
    const auto compile_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - compile_start).count();
    UniformDrawExecutor::Result result;
    std::vector<PackedColor> reference_pixels;
    for (uint32_t iteration = 0; iteration < repeats; ++iteration) {
        result = executor.execute(inputs, cb, pixel_cb, {view[0], view[1], view[2], view[3], view[4], view[5]}, pixels);
        for (size_t i = 0; i < inputs.size(); ++i) {
            const auto expected = reference_vertex(vs, inputs[i], cb);
            for (unsigned r = 0; r < 16; ++r) for (unsigned c = 0; c < 4; ++c) if (vs.outputs[r] & (1u << c))
                nr::capture::check(result.vertices[i][r][c] == expected[r][c]);
        }
        VertexRegisters uniform{};
        for (unsigned r = 0; r < 16; ++r) if (ps.inputs[r] && r != ps.position_input) uniform[r] = result.vertices[0][r];
        nr::capture::check(result.fragment == reference_vertex(ps, uniform, pixel_cb)[0]);
        if (!iteration) reference_pixels = result.pixels; else nr::capture::check(result.pixels == reference_pixels);
        const auto &t = result.timings;
        std::printf("{\"kind\":\"draw_stage_timing\",\"iteration\":%u,\"batch\":%u,\"compile_ms\":%.3f,\"shader_ms\":%.3f,\"projection_ms\":%.3f,\"coverage_ms\":%.3f,\"merger_ms\":%.3f,\"draw_ms\":%.3f,\"rectangle_path\":%s,\"execution_device\":\"%s\"}\n",
            iteration, batch, compile_ms, t.shader_ms, t.projection_ms, t.coverage_ms, t.merger_ms, t.total_ms, result.used_rectangle ? "true" : "false", result.execution_device.c_str());
    }
    pixels = std::move(result.pixels);
    std::ofstream output(argv[6], std::ios::binary); nr::capture::check(bool(output));
    output.write(reinterpret_cast<const char *>(pixels.data()), std::streamsize(pixels.size() * sizeof(PackedColor))); output.close(); nr::capture::check(bool(output));
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("{\"kind\":\"offline_uniform_draw_replay\",\"width\":%u,\"height\":%u,\"vertices\":%u,\"triangles\":%u,\"covered_samples\":%llu,\"tile_executions\":%llu,\"execution_device\":\"%s\",\"elapsed_ms\":%lld,\"displayed_frames\":0,\"desktop_offload_tested\":false}\n",
        width, height, args[0], args[0] / 3, static_cast<unsigned long long>(result.covered_samples), static_cast<unsigned long long>(result.tile_executions),
        result.execution_device.c_str(), static_cast<long long>(milliseconds));
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
