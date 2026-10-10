#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace nr::shader {
// Decoded float texels, one mip, clamp or mirrored 2D sampling. Resource format
// decoding and D3D sampler-state validation belong to the graphics boundary.
// UINT resources transport unchanged 32-bit lane representations in the same
// storage; consumers use memcpy, never a numerical float conversion.
struct TextureShape { uint32_t width = 0, height = 0, components = 15; };
enum class TextureFilter { point, linear };
enum class TextureAddress { clamp, mirror };
struct TextureSampler {
    TextureFilter filter = TextureFilter::linear;
    TextureAddress u = TextureAddress::clamp, v = TextureAddress::clamp;
};
using TextureShapes = std::array<TextureShape, 16>;
using TextureSamplers = std::array<TextureSampler, 16>;
using TexturePixels = std::array<std::vector<std::array<float, 4>>, 16>;
}
