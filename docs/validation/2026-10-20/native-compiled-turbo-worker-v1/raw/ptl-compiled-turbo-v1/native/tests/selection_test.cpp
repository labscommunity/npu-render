#include "selection.hpp"
#include <iostream>

int main() {
    try {
        if (nr::parse_selection("{\n  \"version\": 1,\n  \"backend\": \"NPU\"\n}\n") != "NPU" ||
            nr::parse_selection("{\"backend\":\"CPU\",\"version\":1}") != "CPU")
            throw std::runtime_error("canonical CLI selection rejected");
        for (const auto invalid : {
            "{\"version\":1,\"backend\":\"AUTO\"}",
            "{\"version\":1,\"backend\":\"N P U\"}",
            "{\"version\":2,\"backend\":\"GPU\"}",
            "{\"version\":1,\"backend\":\"GPU.0\"}",
            "{\"version\":1,\"backend\":\"NPU\",\"backend\":\"CPU\"}",
            "{\"version\":1,\"backend\":\"NPU\"}junk",
            "{\"version\":1}", "[]"}) {
            bool rejected = false;
            try { nr::parse_selection(invalid); } catch (const std::invalid_argument &) { rejected = true; }
            if (!rejected) throw std::runtime_error("invalid backend policy accepted");
        }
        std::cout << "native selection contract passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
