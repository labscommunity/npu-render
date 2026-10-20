// Pointer-free geometry for bounded copies between different linear layouts.
#pragma once
namespace nr::kmd {
struct RowCopy {
    ULONG row_bytes = 0, rows = 0, source_pitch = 0, destination_pitch = 0;
    ULONGLONG source_span() const { return rows ? ULONGLONG(rows - 1) * source_pitch + row_bytes : 0; }
    ULONGLONG destination_span() const { return rows ? ULONGLONG(rows - 1) * destination_pitch + row_bytes : 0; }
    bool valid() const {
        return row_bytes && rows && rows <= 8192 && source_pitch >= row_bytes && destination_pitch >= row_bytes &&
               source_span() <= 128ull * 1024 * 1024 && destination_span() <= 128ull * 1024 * 1024;
    }
};
static_assert(sizeof(RowCopy) == 16);
}
