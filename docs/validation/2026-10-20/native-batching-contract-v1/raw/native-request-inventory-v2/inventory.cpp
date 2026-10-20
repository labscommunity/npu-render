#include "indexed_exchange_protocol.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <limits>
namespace ex = nr::registered_lab::indexed_exchange;
static std::string digest(const void *data, size_t size) {
    if (size > (std::numeric_limits<ULONG>::max)()) throw std::runtime_error("hash input limit");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("hash provider");
    unsigned char bytes[32]{};
    unsigned char empty = 0;
    const auto status = BCryptHash(algorithm, nullptr, 0,
        size ? const_cast<PUCHAR>(static_cast<const unsigned char *>(data)) : &empty,
        ULONG(size), bytes, ULONG(sizeof(bytes)));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("hash execution");
    std::ostringstream result;
    for (auto byte : bytes) result << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return result.str();
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 3) throw std::runtime_error("capture directory and output file required");
        const std::filesystem::path capture(argv[1]), output(argv[2]);
        if (std::filesystem::exists(output)) throw std::runtime_error("output already exists");
        std::ofstream stream(output, std::ios::binary);
        if (!stream) throw std::runtime_error("output unavailable");
        for (unsigned ticket = 0; ticket < 972; ++ticket) {
            std::wostringstream name;
            name << L"request-" << std::setw(8) << std::setfill(L'0') << ticket << L".bin";
            std::ifstream file(capture / name.str(), std::ios::binary | std::ios::ate);
            if (!file) throw std::runtime_error("packet missing");
            const auto size = file.tellg();
            if (size < std::streamoff(sizeof(ex::Header)) || size > std::streamoff(ex::max_packet_bytes))
                throw std::runtime_error("packet size");
            std::vector<uint8_t> bytes(size_t(size), 0);
            file.seekg(0); file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
            if (!file) throw std::runtime_error("packet truncated");
            const auto r = ex::decode(bytes);
            const auto &h = r.header;
            stream << "{\"ticket\":" << ticket << ",\"packet_bytes\":" << bytes.size()
                   << ",\"packet_sha256\":\"" << digest(bytes.data(), bytes.size())
                   << "\",\"pixel_sha256\":\"" << digest(r.pixel.bytecode.data(), r.pixel.bytecode.size()*4)
                   << "\",\"vertex_sha256\":\"" << digest(r.vertex.bytecode.data(), r.vertex.bytecode.size()*4)
                   << "\",\"abi\":" << h.abi << ",\"flags\":" << h.flags
                   << ",\"width\":" << h.width << ",\"height\":" << h.height
                   << ",\"vertices\":" << h.vertices << ",\"background_pixels\":" << r.background.size()
                   << ",\"clear\":" << (ex::color_clear(h)?"true":"false") << ",\"resources\":[";
            bool comma=false;
            const auto emit=[&](unsigned slot, const nr::raster::PackedTexture &texture, const nr::shader::TextureSampler &sampler) {
                if (!texture.width && !texture.height && texture.pixels.empty()) return;
                if (comma) stream << ',';
                comma=true;
                stream << "{\"slot\":" << slot << ",\"width\":" << texture.width
                       << ",\"height\":" << texture.height << ",\"format\":" << texture.format
                       << ",\"pixels\":" << texture.pixels.size()
                       << ",\"filter\":" << unsigned(sampler.filter)
                       << ",\"address_u\":" << unsigned(sampler.u) << ",\"address_v\":" << unsigned(sampler.v) << '}';
            };
            if (h.flags & 0x2000) {
                for (unsigned slot=0; slot<16; ++slot) emit(slot, r.resources[slot], r.samplers[slot]);
            } else if (h.flags & 2) {
                // The legacy sampler is encoded in the header, not r.samplers.
                auto sampler=nr::shader::TextureSampler{};
                sampler.filter=(h.flags&8)?nr::shader::TextureFilter::linear:nr::shader::TextureFilter::point;
                sampler.u=sampler.v=nr::shader::TextureAddress::clamp;
                emit(0,r.texture,sampler);
            }
            stream << "],\"vertex_constant_vectors\":[";
            for (size_t i=0;i<r.vertex_constants.size();++i) {if(i)stream<<',';stream<<r.vertex_constants[i].size();}
            stream << "],\"pixel_constant_vectors\":[";
            for (size_t i=0;i<r.pixel_constants.size();++i) {if(i)stream<<',';stream<<r.pixel_constants[i].size();}
            stream << "],\"resource_identity_available\":false,\"pixels_exported\":false,\"npu_executed\":false}\n";
            if (!stream) throw std::runtime_error("inventory output failed");
        }
        return 0;
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
