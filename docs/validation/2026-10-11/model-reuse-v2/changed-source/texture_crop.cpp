#include "provider_compile.hpp"
#include "texture_crop.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <cstring>
#include <sstream>
#include <map>
#include <stdexcept>
namespace nr::raster {
namespace {
namespace op = ov::opset13;
using Node = std::shared_ptr<ov::Node>;
void require(bool value) { if (!value) throw std::invalid_argument("unsupported point texture crop"); }
Node integer(int32_t n) { return op::Constant::create(ov::element::i32, ov::Shape{}, {n}); }
Node lane(const Node &v, int64_t n, int32_t axis) {
    return std::make_shared<op::Gather>(v, op::Constant::create(ov::element::i64, ov::Shape{1}, {n}), integer(axis));
}
}
struct TextureCropExecutor::State {
    uint32_t sw, sh, width, height; std::string backend; bool swap;
    ov::Core core; bool failed = false;
    struct Model {
        nr::provider::CompileOwner compile_owner; ov::CompiledModel compiled; ov::InferRequest request; std::string device;
        Model(State &s, uint32_t x, uint32_t y) {
            auto source = std::make_shared<op::Parameter>(ov::element::u8, ov::Shape{s.sh, s.sw, 4});
            source->output(0).get_tensor().set_names({"source"});
            const auto values = [](std::initializer_list<int64_t> v) { return op::Constant::create(ov::element::i64, ov::Shape{v.size()}, std::vector<int64_t>(v)); };
            // Static Slice lets the NPU compile a bounded strided crop. A giant
            // per-pixel dynamic Gather hung this device; it is not used here.
            auto sampled = std::make_shared<op::Slice>(source, values({y,x,0}), values({y+s.height,x+s.width,3}), values({1,1,1}), values({0,1,2}));
            ov::OutputVector channels;
            for (int64_t c = 0; c < 3; ++c) channels.push_back(lane(sampled, s.swap ? 2-c : c, 2));
            channels.push_back(op::Constant::create(ov::element::u8, ov::Shape{s.height,s.width,1}, {uint8_t(255)}));
            auto model = std::make_shared<ov::Model>(ov::OutputVector{std::make_shared<op::Concat>(channels,2)}, ov::ParameterVector{source});
            ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
            if (s.backend == "NPU") {
                for (const auto &node : model->get_ops()) node->get_rt_info()["precise_0"] = std::string{};
                std::ostringstream xml, weights; ov::pass::Serialize serializer(xml, weights); serializer.run_on_model(model);
                const auto bytes = weights.str(); ov::Tensor data(ov::element::u8, ov::Shape{bytes.size()});
                if (!bytes.empty()) std::memcpy(data.data(), bytes.data(), bytes.size());
                model = s.core.read_model(xml.str(), data);
                for (const auto &node : model->get_ops()) require(nr::provider::precision_preserved(node));
                properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
            }
            compiled = nr::provider::compile_model(s.core, model, s.backend, properties, compile_owner); const auto devices = compiled.get_property(ov::execution_devices);
            require(devices.size() == 1 && (devices[0] == s.backend || devices[0].rfind(s.backend + ".",0) == 0));
            device = devices[0]; request = compiled.create_infer_request();
        }
    };
    std::map<uint64_t,std::unique_ptr<Model>> models;
    State(uint32_t source_width, uint32_t source_height, uint32_t w, uint32_t h, const std::string &selected, bool exchange)
        : sw(source_width), sh(source_height), width(w), height(h), backend(selected), swap(exchange) {
        require(sw && sw <= 8192 && sh && sh <= 8192 && uint64_t(sw)*sh <= 4*1024*1024 && width && width <= sw && height && height <= sh);
        require(backend == "CPU" || backend == "GPU" || backend == "NPU");
    }
};
TextureCropExecutor::TextureCropExecutor(uint32_t sw, uint32_t sh, uint32_t w, uint32_t h, const std::string &backend, bool swap)
    : state(std::make_unique<State>(sw, sh, w, h, backend, swap)) {}
TextureCropExecutor::~TextureCropExecutor() = default;
TextureCropExecutor::Result TextureCropExecutor::execute(uint32_t x, uint32_t y, const std::vector<PackedColor> &source) {
    auto &s = *state;
    require(!s.failed && source.size() == size_t(s.sw) * s.sh && x <= s.sw - s.width && y <= s.sh - s.height);
    s.failed = true;
    const auto key = (uint64_t(x) << 32) | y;
    if (!s.models.count(key)) {
        if (s.models.size() >= 8) s.models.erase(s.models.begin());
        s.models.emplace(key, std::make_unique<State::Model>(s,x,y));
    }
    auto &model = *s.models.at(key);
    std::memcpy(model.request.get_tensor("source").data<uint8_t>(), source.data(), source.size() * sizeof(PackedColor));
    model.request.infer(); const auto output = model.request.get_output_tensor();
    require(output.get_element_type() == ov::element::u8 && output.get_byte_size() == size_t(s.width) * s.height * sizeof(PackedColor));
    Result result{std::vector<PackedColor>(size_t(s.width) * s.height), model.device};
    std::memcpy(result.pixels.data(), output.data<const uint8_t>(), output.get_byte_size()); s.failed = false; return result;
}
}
