#pragma once
// The captured DWM shader inventory as vertex/pixel pairs. frame_kernel_test
// generates frame_programs.h from this order; the worker maps batch shaders to
// native programs through the same order.
#include "captured_dwm_shaders.hpp"
#include <cstddef>
#include <cstdint>
#include <iterator>
namespace nr::frame {
struct InventoryPair { const uint32_t *vs; size_t vs_words; const uint32_t *ps; size_t ps_words; const char *sha; };
#define NR_PAIR(n, sha) InventoryPair{vertex##n, std::size(vertex##n), pixel##n, std::size(pixel##n), sha}
inline const InventoryPair inventory_pairs[]{
    NR_PAIR(0, "d8e0b497"), NR_PAIR(1, "595296ba"), NR_PAIR(2, "0a7b1c69"), NR_PAIR(3, "c71638aa"), NR_PAIR(4, "39799859"), NR_PAIR(5, "e3ebf67c"),
    NR_PAIR(6, "b92ae2a7"), NR_PAIR(7, "4e5ed0f8"), NR_PAIR(8, "fbe68f57"), NR_PAIR(9, "b7f4e6d0"), NR_PAIR(10, "95b1c4fb"), NR_PAIR(11, "d056c6dc"),
    NR_PAIR(12, "d305ca5e"), NR_PAIR(13, "7c5bc4d8"), NR_PAIR(14, "e455f7a5"), NR_PAIR(15, "ec1e65f6"), NR_PAIR(16, "20e56de0"), NR_PAIR(17, "3e6229d4"),
    NR_PAIR(18, "2a28d3fe"), NR_PAIR(19, "691da1da"), NR_PAIR(20, "c58da199"), NR_PAIR(21, "0daddc23"), NR_PAIR(22, "5a764502"), NR_PAIR(23, "7823741a"),
    NR_PAIR(24, "e87ed647"), NR_PAIR(25, "0324b34c")};
#undef NR_PAIR
}
