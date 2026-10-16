#include <windows.h>
#include <dxgi.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    const auto module = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) return 1;
    bool passed = true;
    for (const auto name : {"OpenAdapter", "OpenAdapter10", "OpenAdapter10_2"}) {
        const auto open = reinterpret_cast<HRESULT(APIENTRY *)(void *)>(GetProcAddress(module, name));
        alignas(16) unsigned char arguments[512];
        std::memset(arguments, 0xa5, sizeof(arguments));
        if (!open || open(nullptr) != E_INVALIDARG || open(arguments) != DXGI_ERROR_UNSUPPORTED)
            passed = false;
        for (auto byte : arguments) if (byte != 0xa5) passed = false;
    }
    if (!FreeLibrary(module)) passed = false;
    std::puts(passed ? "Lab UMD rejects all adapter opens without changing arguments" : "Lab UMD guard failed");
    return passed ? 0 : 1;
}
