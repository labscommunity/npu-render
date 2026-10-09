// Direct public KMT calls, without the software UMD's replacement exports.
// Exercise mode is restricted to the owned disposable graphics guest and the
// exact paired kernel identity. A lock round trip alone does not prove paging.
#define NOMINMAX
#include <windows.h>
#include <d3dkmthk.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>
#include "../kmd/contract.hpp"

bool record(const char *operation, NTSTATUS status) {
    std::printf("{\"operation\":\"%s\",\"status\":\"%08lx\"}\n", operation, ULONG(status));
    std::fflush(stdout);
    return status == 0;
}
bool guest() {
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1]{}, volume[64]{};
    DWORD count = MAX_COMPUTERNAME_LENGTH + 1;
    return GetComputerNameW(name, &count) && std::wcscmp(name, L"NR-GFX-LAB") == 0 &&
        GetVolumeInformationW(L"C:\\", volume, 64, nullptr, nullptr, nullptr, nullptr, 0) &&
        std::wcscmp(volume, L"NR_WINDOWS") == 0;
}
struct Enumeration {
    std::vector<D3DKMT_ADAPTERINFO> adapters;
    ULONG count = 0;
    bool close() {
        bool ok = true;
        for (ULONG i = 0; i < count; ++i) {
            if (!adapters[i].hAdapter) continue;
            D3DKMT_CLOSEADAPTER args{adapters[i].hAdapter};
            if (record("close_adapter", D3DKMTCloseAdapter(&args))) adapters[i].hAdapter = 0;
            else ok = false;
        }
        return ok;
    }
    ~Enumeration() { close(); }
    bool open() {
        D3DKMT_ENUMADAPTERS2 args{};
        if (!record("enum_count", D3DKMTEnumAdapters2(&args))) return false;
        if (!args.NumAdapters || args.NumAdapters > 256) return false;
        adapters.resize(args.NumAdapters);
        args.pAdapters = adapters.data();
        if (!record("enum_adapters", D3DKMTEnumAdapters2(&args))) return false;
        if (args.NumAdapters > adapters.size()) return false;
        count = args.NumAdapters;
        return true;
    }
};
struct Allocation {
    D3DKMT_HANDLE device = 0, allocation = 0;
    bool locked = false;
    bool unlock() {
        if (!locked) return true;
        D3DKMT_UNLOCK args{};
        args.hDevice = device;
        args.NumAllocations = 1;
        args.phAllocations = &allocation;
        if (!record("unlock", D3DKMTUnlock(&args))) return false;
        locked = false;
        return true;
    }
    bool close() {
        bool ok = unlock();
        if (allocation) {
            D3DKMT_DESTROYALLOCATION args{};
            args.hDevice = device;
            args.AllocationCount = 1;
            args.phAllocationList = &allocation;
            if (record("destroy_allocation", D3DKMTDestroyAllocation(&args))) allocation = 0;
            else ok = false;
        }
        if (device) {
            D3DKMT_DESTROYDEVICE args{device};
            if (record("destroy_device", D3DKMTDestroyDevice(&args))) {
                device = allocation = 0;
                locked = false;
            }
            else ok = false;
        }
        return ok;
    }
    ~Allocation() { close(); }
    bool exercise(D3DKMT_HANDLE adapter) {
        D3DKMT_CREATEDEVICE create_device{};
        create_device.hAdapter = adapter;
        if (!record("create_device", D3DKMTCreateDevice(&create_device))) return false;
        device = create_device.hDevice;
        nr::kernel_contract::Surface surface{};
        surface.width = 64; surface.height = 48; surface.format = 87;
        surface.row_pitch = 256; surface.allocation_bytes = 12288;
        D3DDDI_ALLOCATIONINFO info{};
        info.pPrivateDriverData = &surface;
        info.PrivateDriverDataSize = sizeof(surface);
        D3DKMT_CREATEALLOCATION create{};
        create.hDevice = device;
        create.NumAllocations = 1;
        create.pAllocationInfo = &info;
        if (!record("create_allocation", D3DKMTCreateAllocation(&create))) return false;
        allocation = info.hAllocation;
        for (unsigned pass = 0; pass < 2; ++pass) {
            D3DKMT_LOCK lock{};
            lock.hDevice = device;
            lock.hAllocation = allocation;
            lock.Flags.LockEntire = 1;
            lock.Flags.DonotEvict = 1;
            lock.Flags.ReadOnly = pass != 0;
            lock.Flags.WriteOnly = pass == 0;
            if (!record(pass ? "lock_read" : "lock_write", D3DKMTLock(&lock))) return false;
            locked = true;
            if (!lock.pData) return false;
            auto bytes = static_cast<unsigned char *>(lock.pData);
            for (size_t i = 0; i < size_t(surface.allocation_bytes); ++i) {
                const auto expected = static_cast<unsigned char>((i * 17 + 31) % 251);
                if (!pass) bytes[i] = expected;
                else if (bytes[i] != expected) return false;
            }
            if (!unlock()) return false;
        }
        std::puts("{\"round_trip_bytes\":12288,\"verified\":true,\"paging_execution_proven\":false}");
        return true;
    }
};
int main(int argc, char **argv) {
    const bool list = argc == 2 && std::strcmp(argv[1], "--list") == 0;
    const bool exercise = argc == 2 && std::strcmp(argv[1], "--exercise") == 0;
    if (!list && !exercise) { std::fputs("usage: nr_paging_probe --list|--exercise\n", stderr); return 2; }
    if (exercise && !guest()) {
        std::puts("{\"passed\":false,\"error\":\"exercise requires the owned disposable guest\"}");
        return 2;
    }
    Enumeration enumeration;
    if (!enumeration.open()) return 1;
    D3DKMT_HANDLE selected = 0;
    unsigned matches = 0;
    for (ULONG i = 0; i < enumeration.count; ++i) {
        nr::kernel_contract::AdapterInfo identity{};
        // Zero defaults so a successful empty reply cannot masquerade as ours.
        std::memset(&identity, 0, sizeof(identity));
        D3DKMT_QUERYADAPTERINFO query{};
        query.hAdapter = enumeration.adapters[i].hAdapter;
        query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
        query.pPrivateDriverData = &identity;
        query.PrivateDriverDataSize = sizeof(identity);
        const auto status = D3DKMTQueryAdapterInfo(&query);
        const bool ours = status == 0 && identity.signature == nr::kernel_contract::magic &&
            identity.abi == nr::kernel_contract::version && identity.bytes == sizeof(identity) && !identity.ready;
        std::printf("{\"adapter_index\":%lu,\"query_status\":\"%08lx\",\"paired_lab_identity\":%s}\n",
                    i, ULONG(status), ours ? "true" : "false");
        if (ours) { selected = query.hAdapter; ++matches; }
    }
    bool passed = list;
    if (exercise && matches == 1) {
        Allocation allocation;
        passed = allocation.exercise(selected);
        passed = allocation.close() && passed;
    }
    passed = enumeration.close() && passed;
    std::printf("{\"passed\":%s,\"matching_adapters\":%u,\"exercise_requested\":%s,\"npu_execution_tested\":false,\"desktop_offload_tested\":false}\n",
                passed ? "true" : "false", matches, exercise ? "true" : "false");
    return passed ? 0 : 1;
}
