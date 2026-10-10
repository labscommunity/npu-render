#pragma once
#include "indexed_exchange_protocol.hpp"
namespace nr::registered_lab::indexed_exchange {
class Pipeline {
    std::unique_ptr<raster::UniformDrawExecutor> executor;
    std::unique_ptr<raster::PointTextureDrawExecutor> texture_executor;
    Request previous;
    Request previous_texture;
public:
    raster::UniformDrawExecutor::Result execute(const Request &request) {
        if (request.header.flags & 2) {
            const auto &h = request.header, &old = previous_texture.header;
            const auto &t = request.texture, &pt = previous_texture.texture;
            require(h.vertices == 6 && h.flags == 0xf02 && h.abi == 2);
            if (!texture_executor || old.width != h.width || old.height != h.height || old.format != h.format ||
                pt.width != t.width || pt.height != t.height || pt.format != t.format ||
                previous_texture.vertex.bytecode != request.vertex.bytecode || previous_texture.pixel.bytecode != request.pixel.bytecode) {
                auto candidate = std::make_unique<raster::PointTextureDrawExecutor>(request.vertex,request.pixel,h.width,h.height,h.format,t.width,t.height,t.format,"NPU");
                texture_executor = std::move(candidate);
                previous_texture = {}; previous_texture.header = h; previous_texture.vertex = request.vertex; previous_texture.pixel = request.pixel;
                previous_texture.texture = {t.width,t.height,t.format,{}};
            }
            return texture_executor->execute(request.inputs,request.vertex_constants,request.viewport,t);
        }
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
