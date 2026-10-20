#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace nr::registered_lab {
// Provider boundary: packed four-channel bytes, independent of D3D allocations.
// Device selection is explicit; compilation or execution never falls back.
class BlendExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    explicit BlendExecutor(const std::string &backend);
    ~BlendExecutor();
    BlendExecutor(const BlendExecutor &) = delete;
    BlendExecutor &operator=(const BlendExecutor &) = delete;
    struct Result { std::vector<uint8_t> pixels; std::string execution_device; };
    Result compose(uint32_t width, uint32_t height, const std::vector<uint8_t> &background,
                   const std::vector<uint8_t> &foreground);
};
}
