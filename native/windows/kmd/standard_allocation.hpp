// Kernel-generated metadata for the two admitted linear color formats.
// No allocation, primary ownership or presentation success is implied here.
#pragma once
namespace nr::kmd {
class StandardAllocation {
public:
    static NTSTATUS describe(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA &a) {
        const bool staging = a.StandardAllocationType == D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE;
        bool cross = false;
        kernel_contract::Surface surface{};
        if (staging) {
            if (!a.pCreateStagingSurfaceData) return STATUS_INVALID_PARAMETER;
            surface.width = a.pCreateStagingSurfaceData->Width; surface.height = a.pCreateStagingSurfaceData->Height;
            surface.format = 88; // The standard presentation staging contract requires X8R8G8B8.
            surface.flags = kernel_contract::presentation_staging_surface;
        } else {
            if (a.StandardAllocationType != D3DKMDT_STANDARDALLOCATION_GDISURFACE) return STATUS_NOT_SUPPORTED;
            if (!a.pCreateGdiSurfaceData) return STATUS_INVALID_PARAMETER;
            const auto input = *a.pCreateGdiSurfaceData;
            cross = input.Type == D3DKMDT_GDISURFACE_TEXTURE_CROSSADAPTER;
            if ((!cross && input.Type != D3DKMDT_GDISURFACE_TEXTURE &&
                 input.Type != D3DKMDT_GDISURFACE_TEXTURE_CPUVISIBLE) || input.Flags.Value) return STATUS_NOT_SUPPORTED;
            surface.width = input.Width; surface.height = input.Height;
            if (input.Format == D3DDDIFMT_A8R8G8B8) surface.format = 87;
            else if (input.Format == D3DDDIFMT_A8B8G8R8) surface.format = 28;
            else return STATUS_NOT_SUPPORTED;
            surface.flags = cross ? kernel_contract::cross_adapter_surface : 0;
        }
        if (!surface.width || surface.width > 8192 || !surface.height || surface.height > 8192)
            return STATUS_INVALID_PARAMETER;
        const UINT mask = cross ? 127 : 255;
        surface.row_pitch = (surface.width * 4 + mask) & ~mask;
        const auto rows = cross ? (ULONGLONG(surface.height) + 3) & ~3ull : surface.height;
        surface.allocation_bytes = (ULONGLONG(surface.row_pitch) * rows + 4095) & ~4095ull;
        if (!kernel_contract::valid(surface)) return STATUS_INVALID_PARAMETER;
        const auto allocation_bytes = a.AllocationPrivateDriverDataSize;
        const auto resource_bytes = a.ResourcePrivateDriverDataSize;
        a.AllocationPrivateDriverDataSize = sizeof(surface);
        a.ResourcePrivateDriverDataSize = staging ? 0 : sizeof(surface);
        if (!a.pAllocationPrivateDriverData && !a.pResourcePrivateDriverData)
            return STATUS_SUCCESS; // Size query must leave the input structure untouched.
        if (!a.pAllocationPrivateDriverData || (!staging && !a.pResourcePrivateDriverData) ||
            (staging && a.pResourcePrivateDriverData))
            return STATUS_INVALID_PARAMETER;
        if (allocation_bytes < sizeof(surface) || (!staging && resource_bytes < sizeof(surface)))
            return STATUS_BUFFER_TOO_SMALL;
        RtlCopyMemory(a.pAllocationPrivateDriverData, &surface, sizeof(surface));
        if (!staging) RtlCopyMemory(a.pResourcePrivateDriverData, &surface, sizeof(surface));
        if (staging) a.pCreateStagingSurfaceData->Pitch = surface.row_pitch;
        else a.pCreateGdiSurfaceData->Pitch = surface.row_pitch;
        return STATUS_SUCCESS;
    }
};
}
