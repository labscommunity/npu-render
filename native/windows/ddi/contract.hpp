#pragma once
#include <cstdint>

// Diagnostics only; rendering clients use the ordinary Direct3D interfaces.
struct NrDdiStats {
    uint32_t size = sizeof(NrDdiStats), version = 1;
    uint64_t adapters = 0, devices = 0, draws = 0, executions = 0;
    uint64_t host_upload_bytes = 0, host_copy_bytes = 0, cpu_clears = 0, rejected = 0;
    uint64_t inference_input_bytes = 0, inference_output_bytes = 0;
    uint64_t software_thunk_calls = 0, rejected_thunk_calls = 0;
    char last_thunk[128]{};
    char device[16]{}, driver[64]{}, last_error[256]{};
};
using NrDdiConfigure = int (*)(const char *);
using NrDdiGetStats = int (*)(NrDdiStats *);

// Separate optional ABI for the experimental shared-resource DLL.
struct NrDdiSharedStats {
    uint32_t size = sizeof(NrDdiSharedStats), version = 1;
    uint64_t created = 0, opened = 0, live_surfaces = 0;
    uint64_t aliases = 0, live_aliases = 0, cleanup_failures = 0;
};
using NrDdiGetSharedStats = int (*)(NrDdiSharedStats *);

inline constexpr char nr_ddi_vertex_shader[] = R"(
float4 main(uint id : SV_VertexID) : SV_POSITION {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
})";
inline constexpr char nr_ddi_pixel_shader[] = R"(
Texture2D<float4> sourceImage : register(t0);
float4 main(float4 position : SV_POSITION) : SV_TARGET {
    return sourceImage.Load(int3(int2(position.xy), 0));
})";
