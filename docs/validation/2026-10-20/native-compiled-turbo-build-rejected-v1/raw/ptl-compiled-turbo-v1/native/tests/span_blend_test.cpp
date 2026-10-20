#include "../providers/packed_blend.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        nr::PackedBlendProvider provider(argc > 1 ? argv[1] : "CPU");
        constexpr size_t count = 1100017;
        std::vector<uint8_t> background(count * 4), foreground(count * 4);
        for (size_t i = 0; i < count; ++i)
            for (size_t c = 0; c < 4; ++c) {
                unsigned a = unsigned(i * 13 % 256);
                background[i * 4 + c] = uint8_t(c == 3 ? 255 : (i * 7 + c * 31) % 256);
                foreground[i * 4 + c] = uint8_t(c == 3 ? a : (i * 17 + c * 41) % (a + 1));
            }
        auto expected = background;
        std::vector<nr::BlendSpan> spans;
        for (const auto &[offset, length] :
             std::vector<std::pair<size_t, size_t>>{{3, 524280}, {524300, 53}, {600000, 500000}}) {
            spans.push_back(
                {foreground.data() + offset * 4, background.data() + offset * 4, length});
            for (size_t i = offset; i < offset + length; ++i)
                for (size_t c = 0; c < 4; ++c)
                    expected[i * 4 + c] = uint8_t(std::min(
                        255u,
                        unsigned(foreground[i * 4 + c]) +
                            (unsigned(expected[i * 4 + c]) * (255 - foreground[i * 4 + 3]) + 127) /
                                255));
        }
        provider.compose_spans(spans);
        unsigned maximum = 0;
        for (size_t i = 0; i < background.size(); ++i)
            maximum = std::max(maximum, unsigned(std::abs(int(background[i]) - int(expected[i]))));
        if (maximum > 1 || provider.dispatches != 2 || provider.processed_pixels != 1024333)
            throw std::runtime_error("span packing differs from independent oracle");
        const auto saved = background;
        bool rejected = false;
        try {
            provider.compose_spans({{nullptr, background.data(), 3}});
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        if (!rejected || background != saved)
            throw std::runtime_error("invalid span changed output");
        std::cout << "{\"passed\":true,\"execution_device\":\"" << provider.execution_device
                  << "\",\"maximum_error\":" << maximum << ",\"dispatches\":" << provider.dispatches
                  << "}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
