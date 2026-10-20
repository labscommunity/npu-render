#pragma once
#include "output_merger.hpp"
#include <memory>
#include <string>
namespace nr::raster {
// Finite float color -> clamped, nearest-even UNORM bytes -> complete image.
// Quantization and image generation run on the explicitly selected provider.
class ClearExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    ClearExecutor(uint32_t width,uint32_t height,uint32_t format,const std::string &backend);
    ~ClearExecutor();
    ClearExecutor(const ClearExecutor&)=delete;
    ClearExecutor &operator=(const ClearExecutor&)=delete;
    struct Result {std::vector<PackedColor> pixels;std::string execution_device;double milliseconds;};
    Result execute(const Color &color);
};
}
