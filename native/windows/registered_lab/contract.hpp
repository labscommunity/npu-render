#pragma once
#include <windows.h>
#include <cstdint>
struct NrFillStats {
    UINT bytes = sizeof(NrFillStats), version = 1;
    uint64_t allocations = 0, fills = 0, staging_copy_bytes = 0;
    uint64_t live_allocations = 0, live_contexts = 0, cleanup_errors = 0, rejected = 0;
    uint64_t last_unsupported_offset = ~uint64_t(0);
};
inline bool nr_fill_guest() {
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1]{}, volume[64]{};
    DWORD count = MAX_COMPUTERNAME_LENGTH + 1;
    return GetComputerNameW(name, &count) && lstrcmpW(name, L"NR-GFX-LAB") == 0 &&
        GetVolumeInformationW(L"C:\\", volume, 64, nullptr, nullptr, nullptr, nullptr, 0) &&
        lstrcmpW(volume, L"NR_WINDOWS") == 0;
}
