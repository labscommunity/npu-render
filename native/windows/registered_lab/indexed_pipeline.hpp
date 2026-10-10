#pragma once
#include "indexed_exchange_protocol.hpp"
namespace nr::registered_lab::indexed_exchange {
class Pipeline {
    std::unique_ptr<raster::UniformDrawExecutor> executor;
    Request previous;
public:
    raster::UniformDrawExecutor::Result execute(const Request &request) {
        const auto &h = request.header, &old = previous.header;
        if (!executor || old.width != h.width || old.height != h.height || old.vertices != h.vertices || old.format != h.format || old.flags != h.flags ||
            previous.vertex.bytecode != request.vertex.bytecode || previous.pixel.bytecode != request.pixel.bytecode) {
            uint32_t batch = 4096; while (uint64_t(h.width) * h.height % batch) batch /= 2;
            auto candidate = std::make_unique<raster::UniformDrawExecutor>(request.vertex, request.pixel, h.vertices, h.width, h.height,
                "NPU", (h.flags & 1) != 0, h.format == 87, uint8_t((h.flags >> 8) & 15), batch);
            executor = std::move(candidate);
            previous = {}; previous.header = h; previous.vertex = request.vertex; previous.pixel = request.pixel;
        }
        return executor->execute(request.inputs, request.vertex_constants, request.pixel_constants, request.viewport, request.background);
    }
};
}
