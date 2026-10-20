#pragma once
#include "../ddi/contract.hpp"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace nr::registered_lab {
inline Microsoft::WRL::ComPtr<ID3DBlob> compile_shader(const char *source, const char *profile) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, error;
    if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", profile,
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error)))
        throw std::runtime_error("composition shader compilation failed");
    return code;
}
inline std::vector<UINT> shader_tokens(const char *source, const char *profile) {
    const auto code = compile_shader(source, profile);
    const auto words = static_cast<const UINT *>(code->GetBufferPointer());
    const size_t count = code->GetBufferSize() / sizeof(UINT);
    if (code->GetBufferSize() % 4 || count < 8 || words[0] != 0x43425844 || words[7] > count - 8)
        throw std::runtime_error("invalid compiled DXBC container");
    for (UINT i = 0; i < words[7]; ++i) {
        const size_t offset = words[8 + i] / 4;
        if (words[8 + i] % 4 || offset > count - 2 || words[offset + 1] % 4 || words[offset + 1] / 4 > count - offset - 2)
            throw std::runtime_error("invalid compiled DXBC chunk");
        if (words[offset] == 0x52444853 || words[offset] == 0x58454853)
            return {words + offset + 2, words + offset + 2 + words[offset + 1] / 4};
    }
    throw std::runtime_error("compiled shader program missing");
}
}
