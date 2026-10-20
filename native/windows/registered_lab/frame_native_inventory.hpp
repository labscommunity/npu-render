#pragma once
// The captured inventory lowered once, shared by the worker (to identify batch
// shader pairs) and host tools (to decide which draws a batch may carry).
#include "frame_inventory.hpp"
#include "frame_kernel.hpp"
#include <array>
#include <cstdio>
#include <iterator>
#include <vector>
namespace nr::frame {
// The captured inventory lowered once: a batch shader pair is native when its
// lowered form matches an inventory program; the kernel blob and the index
// inside that blob follow from the inventory order (blob_of).
struct NativeIdentity { uint32_t blob = UINT32_MAX, blob_index = 0, host_index = 0; };
class NativeInventory {
    struct Entry { LoweredProgram lowered; uint32_t pair = 0; NativeIdentity identity; };
    std::vector<Entry> entries;
public:
    NativeInventory() {
        std::array<uint32_t, blob_ranges.size()> counts{};
        for (size_t n = 0; n < std::size(inventory_pairs); ++n) {
            const auto &pair = inventory_pairs[n];
            shader::Program vs, ps;
            try { vs = shader::parse_vertex(pair.vs, pair.vs_words); ps = shader::parse_pixel(pair.ps, pair.ps_words); } catch (const std::exception &) { continue; }
            Entry e; e.lowered = lower(vs, ps, shader::TextureSamplers{}); e.pair = uint32_t(n);
            if (!e.lowered.eligible()) continue;
            e.identity.blob = blob_of(uint32_t(e.lowered.ps_words.size()));
            require(e.identity.blob < counts.size());
            e.identity.blob_index = counts[e.identity.blob]++;
            e.identity.host_index = uint32_t(entries.size());
            // The compiled host header must list the same programs in the same order.
            require(e.identity.host_index < NR_NATIVE_COUNT && core::nr_native_inventory(e.identity.host_index) == e.pair);
            entries.push_back(std::move(e));
        }
        require(NR_NATIVE_COUNT == 2 * entries.size());
        std::printf("{\"native_inventory\":true,\"programs\":%zu}\n", entries.size()); std::fflush(stdout);
    }
    // Host header index of the program at `blob_index` inside kernel blob `blob`; 0xffffffff when none.
    uint32_t host_index(uint32_t blob, uint32_t blob_index) const {
        uint32_t real = 0; for (const auto &e : entries) real += e.identity.blob == blob ? 1u : 0u;
        const bool twin = blob_index >= real; if (twin) blob_index -= real;
        for (const auto &e : entries) if (e.identity.blob == blob && e.identity.blob_index == blob_index) return e.identity.host_index + (twin ? uint32_t(entries.size()) : 0u);
        return 0xffffffffu;
    }
    const NativeIdentity *find(const LoweredProgram &p) const {
        for (const auto &e : entries) {
            const auto &q = e.lowered;
            if (q.vs_words == p.vs_words && q.ps_words == p.ps_words && q.vs_immediates == p.vs_immediates && q.ps_immediates == p.ps_immediates &&
                q.vs_constant_base == p.vs_constant_base && q.ps_constant_base == p.ps_constant_base && q.vs_constant_count == p.vs_constant_count && q.ps_constant_count == p.ps_constant_count &&
                q.vs_inputs == p.vs_inputs && q.ps_inputs == p.ps_inputs && q.position_output == p.position_output && q.interpolation == p.interpolation && q.texture_slots == p.texture_slots)
                return &e.identity;
        }
        return nullptr;
    }
};
}
