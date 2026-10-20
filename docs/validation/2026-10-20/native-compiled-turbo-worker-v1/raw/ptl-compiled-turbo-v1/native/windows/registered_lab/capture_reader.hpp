#pragma once
// Read-only laboratory capture parser. Never accepts a partial capture as a draw.
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace nr::capture {
inline void check(bool value) { if (!value) throw std::invalid_argument("invalid or unsupported draw capture"); }
class Snapshot {
    std::map<std::string, std::vector<uint8_t>> records;
public:
    explicit Snapshot(const char *path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        check(bool(file)); const auto size = file.tellg(); check(size >= 48 && size <= 96 * 1024 * 1024);
        file.seekg(0); char magic[8]{}; file.read(magic, 8); check(!std::memcmp(magic, "NPDRAW01", 8));
        uint64_t remaining = uint64_t(size) - 8; bool complete = false;
        while (remaining) {
            check(remaining >= 40 && records.size() < 128 && !complete);
            char label[32]{}; uint64_t bytes = 0;
            file.read(label, 32); file.read(reinterpret_cast<char *>(&bytes), 8); remaining -= 40;
            check(bool(file) && label[0] && label[31] == 0 && bytes <= remaining);
            const std::string name(label);
            for (const auto c : name) check((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-');
            check(!records.count(name));
            auto &data = records[name]; data.resize(size_t(bytes));
            if (bytes) file.read(reinterpret_cast<char *>(data.data()), std::streamsize(bytes));
            check(bool(file)); remaining -= bytes;
            complete = name == "complete"; if (complete) check(!remaining && !bytes);
        }
        check(complete);
    }
    bool has(const std::string &name) const { return records.count(name) != 0; }
    const std::vector<uint8_t> &bytes(const std::string &name) const {
        auto it = records.find(name); check(it != records.end()); return it->second;
    }
    template<class T> std::vector<T> values(const std::string &name) const {
        const auto &data = bytes(name); check(data.size() % sizeof(T) == 0);
        std::vector<T> result(data.size() / sizeof(T));
        if (!data.empty()) std::memcpy(result.data(), data.data(), data.size()); return result;
    }
};
}
