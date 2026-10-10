#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace nr::shader {
// Decoded float texels, one mip, edge-clamped 2D sampling. Resource format
// decoding and D3D sampler-state validation belong to the graphics boundary.
struct TextureShape { uint32_t width = 0, height = 0; };
enum class TextureFilter { point, linear };
struct TextureSampler { TextureFilter filter = TextureFilter::linear; };
using TextureShapes = std::array<TextureShape, 16>;
using TextureSamplers = std::array<TextureSampler, 16>;
using TexturePixels = std::array<std::vector<std::array<float, 4>>, 16>;
}
