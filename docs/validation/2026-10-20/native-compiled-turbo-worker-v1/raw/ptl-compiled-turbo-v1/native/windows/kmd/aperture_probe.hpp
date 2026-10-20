// Opt-in owned-memory diagnostic. No aperture is advertised to Windows and
// no Windows fence is synthesized. Adapter startup remains disabled.
#pragma once
#include "aperture.hpp"
namespace nr::kmd {
struct ApertureProbe {
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    ULONG phase = 0;
    SIZE_T verified_bytes = 0, released_pages = 0;
    bool cleaned = false;
    void run() {
        constexpr ULONG tag = 'pApN';
        constexpr SIZE_T image_bytes = (SIZE_T(2880) * 4 * 1800 + PAGE_SIZE - 1) & ~SIZE_T(PAGE_SIZE - 1);
        constexpr SIZE_T source_bytes = image_bytes + PAGE_SIZE;
        Aperture aperture;
        UCHAR *source = nullptr, *dummy = nullptr;
        PMDL mdl = nullptr;
        status = STATUS_UNSUCCESSFUL;
        do {
            phase = 1;
            status = aperture.prepare();
            if (status != STATUS_SUCCESS) break;
            phase = 2;
            source = static_cast<UCHAR *>(ExAllocatePool2(POOL_FLAG_NON_PAGED, source_bytes, tag));
            dummy = static_cast<UCHAR *>(ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, tag));
            if (!source || !dummy) { status = STATUS_INSUFFICIENT_RESOURCES; break; }
            if ((ULONG_PTR(source) % PAGE_SIZE) || (ULONG_PTR(dummy) % PAGE_SIZE)) {
                status = STATUS_INVALID_ADDRESS; break;
            }
            RtlFillMemory(source, source_bytes, 0x22); RtlFillMemory(dummy, PAGE_SIZE, 0xc3);
            phase = 3;
            mdl = IoAllocateMdl(source, ULONG(source_bytes), FALSE, FALSE, nullptr);
            if (!mdl) { status = STATUS_INSUFFICIENT_RESOURCES; break; }
            MmBuildMdlForNonPagedPool(mdl);
            constexpr SIZE_T first = 7, count = image_bytes / PAGE_SIZE;
            status = aperture.map(this, first, count, mdl, 1, 1);
            if (status != STATUS_SUCCESS) break;
            IoFreeMdl(mdl); mdl = nullptr; // Pages remain owned; input descriptor does not.
            phase = 4;
            status = aperture.fill(first * PAGE_SIZE + 13, image_bytes - 16, 0xa5a5a5a5);
            if (status != STATUS_SUCCESS) break;
            bool equal = true;
            for (SIZE_T i = 0; i < source_bytes; ++i) {
                const auto expected = i >= PAGE_SIZE + 13 && i < PAGE_SIZE + image_bytes - 3 ? 0xa5 : 0x22;
                if (source[i] != expected) { equal = false; break; }
            }
            if (!equal) { status = STATUS_DATA_ERROR; break; }
            verified_bytes = source_bytes;
            phase = 5;
            UCHAR readback[64]{};
            status = aperture.read((first + 1) * PAGE_SIZE - 17, readback, sizeof(readback));
            if (status != STATUS_SUCCESS) break;
            for (const auto value : readback) if (value != 0xa5) equal = false;
            RtlFillMemory(readback, sizeof(readback), 0x7f);
            status = aperture.write((first + 2) * PAGE_SIZE - 19, readback, sizeof(readback));
            if (status != STATUS_SUCCESS) break;
            for (SIZE_T i = 0; i < sizeof(readback); ++i)
                if (source[3 * PAGE_SIZE - 19 + i] != 0x7f) equal = false;
            if (!equal) { status = STATUS_DATA_ERROR; break; }
            phase = 6;
            const auto physical = MmGetPhysicalAddress(dummy);
            status = aperture.unmap(this, first, count, physical);
            if (status != STATUS_SUCCESS) break;
            if (aperture.mapped_pages() || aperture.dummy_pages() != count) { status = STATUS_DATA_ERROR; break; }
            released_pages = count;
            phase = 7;
            status = aperture.read((first + 1) * PAGE_SIZE - 17, readback, sizeof(readback));
            if (status != STATUS_SUCCESS) break;
            for (const auto value : readback) if (value != 0xc3) equal = false;
            status = aperture.fill((first + 1) * PAGE_SIZE - 16, 32, 0x6b6b6b6b);
            if (status != STATUS_SUCCESS) break;
            for (SIZE_T i = 0; i < PAGE_SIZE; ++i)
                if (dummy[i] != ((i < 16 || i >= PAGE_SIZE - 16) ? 0x6b : 0xc3)) equal = false;
            if (!equal) { status = STATUS_DATA_ERROR; break; }
            phase = 8;
            status = STATUS_SUCCESS;
        } while (false);
        aperture.release(); // Drop translations before releasing their owned pages.
        if (mdl) IoFreeMdl(mdl);
        if (dummy) ExFreePoolWithTag(dummy, tag);
        if (source) ExFreePoolWithTag(source, tag);
        cleaned = !aperture.ready();
    }
};
} // namespace nr::kmd
