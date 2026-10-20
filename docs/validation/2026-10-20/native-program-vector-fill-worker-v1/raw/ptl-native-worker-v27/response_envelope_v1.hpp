#pragma once
#include "indexed_exchange_protocol.hpp"
namespace nr::registered_lab::indexed_exchange {
// Validation prefix copied from the pinned decoder. response_valid already
// verifies exact pixel extent; no pixel values are inspected by that decoder.
inline Receipt validate_response_envelope(const std::vector<uint8_t> &bytes, const Header &request) {
    Reader reader(bytes); const auto h = reader.value<Header>();
    if (failure_valid(h, request, bytes.size())) throw std::runtime_error("physical NPU worker rejected indexed draw");
    require(response_valid(h, request, bytes.size())); const auto receipt = reader.value<Receipt>();
    require(!receipt.reserved && receipt.used_rectangle <= 1 && receipt.provider_requests && receipt.provider_requests <= 4 * 1024 * 1024 &&
        receipt.covered_samples <= uint64_t(h.width) * h.height * (color_clear(h)?1u:h.vertices/3));
    if(color_clear(h))require(receipt.input_bytes==16 && receipt.output_bytes==uint64_t(h.width)*h.height*4 &&
        receipt.provider_requests==1 && receipt.tile_executions==1 && receipt.covered_samples==uint64_t(h.width)*h.height && !receipt.used_rectangle);
    return receipt;
}
}
