#include "provider_compile.hpp"
#include "output_merger.hpp"
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/pass/serialize.hpp>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace nr::raster {
namespace {
namespace op = ov::opset13;
using Node = std::shared_ptr<ov::Node>;
void require(bool value) { if (!value) throw std::invalid_argument("unsupported output-merger request"); }
Node integer(int32_t value) { return op::Constant::create(ov::element::i32, ov::Shape{}, {value}); }
Node number(float value) { return op::Constant::create(ov::element::f32, ov::Shape{}, {value}); }
Node lane(const Node &v, int64_t i) {
    return std::make_shared<op::Gather>(v, op::Constant::create(ov::element::i64, ov::Shape{1}, {i}), integer(1));
}
Node add(const Node &a, const Node &b) { return std::make_shared<op::Add>(a, b); }
Node sub(const Node &a, const Node &b) { return std::make_shared<op::Subtract>(a, b); }
Node mul(const Node &a, const Node &b) { return std::make_shared<op::Multiply>(a, b); }
Node both(const Node &a, const Node &b) { return std::make_shared<op::BitwiseAnd>(a, b); }
Node pred(const Node &a) { return std::make_shared<op::Convert>(a, ov::element::i32); }
}
struct OutputMerger::State {
    uint32_t count;
    BlendMode blend;
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    std::string execution_device;
    bool failed = false;
    State(uint32_t size, const std::string &backend, BlendMode blending, bool bgra, uint8_t mask) : count(size), blend(blending) {
        require(blend == BlendMode::source_copy || blend == BlendMode::source_over || blend == BlendMode::destination_in);
        require(count && count <= 65536 && mask && mask <= 15 && (backend == "NPU" || backend == "CPU" || backend == "GPU"));
        auto colors = std::make_shared<op::Parameter>(ov::element::f32, ov::Shape{count, 4});
        auto background = std::make_shared<op::Parameter>(ov::element::u8, ov::Shape{count, 4});
        auto coverage = std::make_shared<op::Parameter>(ov::element::i32, ov::Shape{count, 1});
        colors->output(0).get_tensor().set_names({"colors"}); background->output(0).get_tensor().set_names({"background"});
        coverage->output(0).get_tensor().set_names({"coverage"});
        std::vector<int32_t> rows(count); for (uint32_t i = 0; i < count; ++i) rows[i] = int32_t(i);
        auto row = op::Constant::create(ov::element::i32, ov::Shape{count}, rows);
        auto shape = op::Constant::create(ov::element::i64, ov::Shape{2}, {int64_t(count), int64_t(1)});
        auto flat_shape = op::Constant::create(ov::element::i64, ov::Shape{1}, {int64_t(count)});
        auto choose = [&](const Node &condition, const Node &yes, const Node &no) -> Node {
            auto index = add(row, mul(std::make_shared<op::Reshape>(pred(std::make_shared<op::LogicalNot>(condition)), flat_shape, false), integer(int32_t(count))));
            auto choices = std::make_shared<op::Concat>(ov::OutputVector{
                std::make_shared<op::Broadcast>(yes, shape), std::make_shared<op::Broadcast>(no, shape)}, 0);
            return std::make_shared<op::Gather>(choices, index, integer(0));
        };
        ov::OutputVector channels;
        // D3D11.3 section 17.5: UNORM inputs are clamped before blending,
        // including alpha used as the destination factor, and again afterward.
        auto clamp_source = [&](const Node &v) -> Node {
            return choose(std::make_shared<op::Less>(v,number(0)),number(0),
                choose(std::make_shared<op::Greater>(v,number(1)),number(1),v));
        };
        auto alpha = clamp_source(lane(colors, 3));
        for (unsigned ch = 0; ch < 4; ++ch) {
            const auto source = bgra && ch < 3 ? 2 - ch : ch;
            auto bc = lane(background, ch);
            if (!(mask & (1u << source))) { channels.push_back(pred(bc)); continue; }
            // Multi-index channel Gather corrupts this mixed-type NPU graph.
            // Separate lanes also prevent flattened clamp selection losing FP32.
            auto fg = clamp_source(lane(colors, source));
            Node value = mul(fg, number(255));
            if (blend == BlendMode::source_over) value = add(value, mul(std::make_shared<op::Convert>(bc, ov::element::f32), sub(number(1), alpha)));
            else if (blend == BlendMode::destination_in) value = mul(std::make_shared<op::Convert>(bc, ov::element::f32), alpha);
            value = choose(std::make_shared<op::Less>(value, number(0)), number(0),
                choose(std::make_shared<op::Greater>(value, number(255)), number(255), value));
            auto base = both(pred(value), integer(255));
            auto fraction = sub(value, std::make_shared<op::Convert>(base, ov::element::f32));
            auto greater = pred(std::make_shared<op::Greater>(fraction, number(.5f)));
            auto equal = pred(std::make_shared<op::Equal>(fraction, number(.5f)));
            auto increment = std::make_shared<op::BitwiseOr>(greater, both(equal, both(base, integer(1))));
            auto quantized = add(base, increment);
            channels.push_back(add(mul(quantized, coverage), mul(pred(bc), sub(integer(1), coverage))));
        }
        auto output = std::make_shared<op::Convert>(std::make_shared<op::Concat>(channels, 1), ov::element::u8);
        auto model = std::make_shared<ov::Model>(ov::OutputVector{output}, ov::ParameterVector{colors, background, coverage});
        ov::AnyMap properties{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
        if (backend == "NPU") {
            for (const auto &node : model->get_ops()) node->get_rt_info()["precise_0"] = std::string{};
            std::ostringstream xml, weights; ov::pass::Serialize serializer(xml, weights); serializer.run_on_model(model);
            const auto bytes = weights.str(); ov::Tensor data(ov::element::u8, ov::Shape{bytes.size()});
            if (!bytes.empty()) std::memcpy(data.data(), bytes.data(), bytes.size());
            model = core.read_model(xml.str(), data);
            for (const auto &node : model->get_ops())
                if (!node->get_rt_info().count("precise_0")) throw std::runtime_error("output precision marker lost");
            properties.emplace(ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        }
        compiled = nr::provider::compile_model(core, model, backend, properties);
        const auto devices = compiled.get_property(ov::execution_devices);
        require(devices.size() == 1 && (devices[0] == backend || devices[0].rfind(backend + ".", 0) == 0));
        execution_device = devices[0]; request = compiled.create_infer_request();
    }
};
OutputMerger::OutputMerger(uint32_t samples, const std::string &backend, bool blend, bool bgra, uint8_t mask)
    : OutputMerger(samples, backend, source_blend(blend), bgra, mask) {}
OutputMerger::OutputMerger(uint32_t samples, const std::string &backend, BlendMode blend, bool bgra, uint8_t mask)
    : state(std::make_unique<State>(samples, backend, blend, bgra, mask)) {}
OutputMerger::~OutputMerger() = default;
OutputMerger::Result OutputMerger::execute(const std::vector<Color> &colors, const std::vector<PackedColor> &background,
                                         const std::vector<uint8_t> &coverage) {
    auto &s = *state;
    require(!s.failed && colors.size() == s.count && background.size() == s.count && coverage.size() == s.count);
    for (size_t i = 0; i < s.count; ++i) {
        require(coverage[i] <= 1);
        for (const auto value : colors[i]) require(std::isfinite(value) && std::abs(value) <= 16);
    }
    s.failed = true;
    auto col = s.request.get_tensor("colors"), bg = s.request.get_tensor("background"), cov = s.request.get_tensor("coverage");
    std::memcpy(col.data<float>(), colors.data(), colors.size() * sizeof(Color));
    std::memcpy(bg.data<uint8_t>(), background.data(), background.size() * sizeof(PackedColor));
    for (size_t i = 0; i < s.count; ++i) cov.data<int32_t>()[i] = coverage[i];
    s.request.infer(); const auto output = s.request.get_output_tensor();
    require(output.get_element_type() == ov::element::u8 && output.get_size() == size_t(s.count) * 4);
    Result result{{}, s.execution_device}; result.pixels.resize(s.count);
    std::memcpy(result.pixels.data(), output.data<const uint8_t>(), result.pixels.size() * sizeof(PackedColor));
    s.failed = false; return result;
}
}
