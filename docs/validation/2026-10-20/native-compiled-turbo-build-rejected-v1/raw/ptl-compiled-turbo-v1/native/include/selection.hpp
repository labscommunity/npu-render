#pragma once
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

namespace nr {
// Consume the small canonical file written atomically by `npu-render select`.
// Device indices and general JSON escapes are deliberately rejected: this
// adapter currently has exactly one qualified GPU, NPU and CPU provider.
inline std::string parse_selection(const std::string &text) {
    if (text.size() > 1024) throw std::invalid_argument("selection exceeds 1024 bytes");
    std::string compact;
    bool quoted = false;
    for (char c : text) {
        if (c == '"') quoted = !quoted;
        if (quoted || (c != ' ' && c != '\t' && c != '\r' && c != '\n')) compact += c;
    }
    for (const std::string backend : {"GPU", "NPU", "CPU"}) {
        const auto value = "\"backend\":\"" + backend + "\"";
        if (compact == "{\"version\":1," + value + "}" || compact == "{" + value + ",\"version\":1}")
            return backend;
    }
    throw std::invalid_argument("expected canonical version 1 selection with GPU, NPU or CPU");
}

class SelectionFile {
    std::filesystem::path path;
    std::string previous;
    bool observed = false;
public:
    explicit SelectionFile(std::filesystem::path destination) : path(std::move(destination)) {}
    std::optional<std::string> changed() {
        if (!std::filesystem::exists(path)) return std::nullopt;
        std::ifstream source(path, std::ios::binary);
        if (!source) throw std::runtime_error("cannot open selection file");
        std::string text(1025, '\0');
        source.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (source.bad()) throw std::runtime_error("cannot read selection file");
        text.resize(static_cast<size_t>(source.gcount()));
        if (observed && text == previous) return std::nullopt;
        previous = text; observed = true;
        return parse_selection(text);
    }
};
}
