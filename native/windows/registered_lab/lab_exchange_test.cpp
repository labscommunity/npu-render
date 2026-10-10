#include "lab_exchange_protocol.hpp"
#include <cstdio>
using namespace nr::registered_lab::exchange;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (false)
int main() {
    Header request{}; request.nonce[0] = 0xabcdef; request.width = 320; request.height = 192;
    request.sequence = 1; request.frame_bytes = 245760;
    CHECK(request_valid(request, 491584));
    CHECK(!request_valid(request, 491583) && !request_valid(request, 491585));
    auto reply = request; reply.kind = 2; reply.device = 3;
    CHECK(response_valid(reply, request, 245824));
    CHECK(!request_valid(reply, 491584));
    for (unsigned i = 0; i < 13; ++i) {
        auto bad = request;
        switch (i) {
        case 0: bad.signature ^= 1; break;
        case 1: bad.abi = 2; break;
        case 2: bad.nonce[0] = 0; break;
        case 3: bad.width = 0; break;
        case 4: bad.width = 8193; break;
        case 5: bad.height = 0; break;
        case 6: bad.height = 0xffffffff; break;
        case 7: bad.sequence = 0; break;
        case 8: bad.sequence = 4; break;
        case 9: bad.frame_bytes--; break;
        case 10: bad.device = 1; break;
        case 11: bad.reserved = 1; break;
        case 12: bad.reserved2 = 1; break;
        }
        CHECK(!request_valid(bad, 491584));
    }
    auto oversized = request; oversized.width = oversized.height = 8192; oversized.frame_bytes = 268435456;
    CHECK(!request_valid(oversized, sizeof(Header) + uint64_t(oversized.frame_bytes) * 2));
    for (unsigned i = 0; i < 8; ++i) {
        auto bad = reply;
        switch (i) {
        case 0: bad.nonce[0]++; break;
        case 1: bad.nonce[1]++; break;
        case 2: bad.sequence = 2; break;
        case 3: bad.width = 192; bad.height = 320; break;
        case 4: bad.device = 1; break; // Reject CPU output under the NPU label.
        case 5: bad.device = 2; break; // Reject GPU output under the NPU label.
        case 6: bad.kind = 1; break;
        case 7: bad.abi = 2; break;
        }
        CHECK(!response_valid(bad, request, 245824));
    }
    CHECK(!response_valid(reply, request, 245823) && !response_valid(reply, request, 245825));
    std::puts("bounded exchange rejects stale identity, malformed extents, wrong devices and trailing data");
    return 0;
}
