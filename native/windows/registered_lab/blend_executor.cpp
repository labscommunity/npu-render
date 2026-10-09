#include "blend_executor.hpp"
#include "../blend_model.hpp"
#include <cstring>
#include <map>
#include <stdexcept>
namespace nr::registered_lab {
struct BlendExecutor::State {
    ov::Core core;
    std::string backend;
    struct Model {
        ov::CompiledModel compiled;
        ov::InferRequest request;
        std::string execution_device;
        Model(ov::Core &core, const std::string &backend, uint32_t width, uint32_t height) {
            compiled = core.compile_model(blend_model(width, height, true, 4, "convolution"), backend,
                ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY));
            const auto devices = compiled.get_property(ov::execution_devices);
            if (devices.size() != 1 || !(devices[0] == backend || devices[0].rfind(backend + ".", 0) == 0))
                throw std::runtime_error("unexpected composition execution device");
            execution_device = devices[0];
            request = compiled.create_infer_request();
        }
    };
    std::map<uint64_t, std::unique_ptr<Model>> models;
    explicit State(const std::string &selected) : backend(selected) {
        if (backend != "CPU" && backend != "GPU" && backend != "NPU")
            throw std::invalid_argument("explicit CPU, GPU or NPU required");
    }
};
BlendExecutor::BlendExecutor(const std::string &backend) : state(std::make_unique<State>(backend)) {}
BlendExecutor::~BlendExecutor() = default;
BlendExecutor::Result BlendExecutor::compose(uint32_t width, uint32_t height,
        const std::vector<uint8_t> &background, const std::vector<uint8_t> &foreground) {
    if (!width || width > 8192 || !height || height > 8192 ||
        uint64_t(width) * height * 4 > 128ull * 1024 * 1024 ||
        background.size() != size_t(width) * height * 4 || foreground.size() != background.size())
        throw std::invalid_argument("invalid packed composition extents");
    const uint64_t key = (uint64_t(width) << 32) | height;
    if (!state->models.count(key)) {
        if (state->models.size() >= 8) throw std::runtime_error("composition shape cache is full");
        state->models.emplace(key, std::make_unique<State::Model>(state->core, state->backend, width, height));
    }
    auto &model = *state->models.at(key);
    try {
        std::memcpy(model.request.get_input_tensor(0).data(), background.data(), background.size());
        std::memcpy(model.request.get_input_tensor(1).data(), foreground.data(), foreground.size());
        model.request.infer();
        const auto output = model.request.get_output_tensor();
        if (output.get_byte_size() != foreground.size()) throw std::runtime_error("unexpected composition output");
        const auto pixels = output.data<const uint8_t>();
        return {{pixels, pixels + foreground.size()}, model.execution_device};
    } catch (...) {
        state->models.erase(key); // Never reuse an infer request after failure.
        throw;
    }
}
}
