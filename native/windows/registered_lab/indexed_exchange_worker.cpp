// Finite physical-NPU filesystem worker. Its controller uses authenticated WinRM.
#define NOMINMAX
#include "indexed_exchange_protocol.hpp"
#include "lab_exchange_io.hpp"
#include <cstdio>
#include <set>
#include <cwchar>
namespace ex = nr::registered_lab::indexed_exchange;
namespace io = nr::registered_lab::exchange;
namespace {
std::wstring packet_name(const ex::Header &h, bool reply) {
    wchar_t name[100]{};
    swprintf_s(name, L"%ls-%u-%016llx%016llx-%u.bin", reply ? L"reply" : L"request", h.client_pid,
        static_cast<unsigned long long>(h.nonce[0]), static_cast<unsigned long long>(h.nonce[1]), h.sequence);
    return name;
}
void publish(const std::wstring &path, const std::vector<uint8_t> &data) {
    const auto temporary = path + L".tmp";
    { io::File file(temporary, true); file.write(data.data(), DWORD(data.size())); file.flush(); }
    io::publish(temporary, path);
}
}
int wmain(int argc, wchar_t **argv) try {
    if (argc != 3) throw std::invalid_argument("expected <owned directory> <maximum requests>");
    const std::wstring directory = argv[1]; wchar_t *end = nullptr; const auto limit = std::wcstoul(argv[2], &end, 10);
    ex::require(end != argv[2] && !*end && limit && limit <= ex::max_requests);
    const auto attributes = GetFileAttributesW(directory.c_str());
    ex::require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT));
    const auto started = GetTickCount64(); std::set<std::wstring> handled;
    std::unique_ptr<nr::raster::UniformDrawExecutor> executor;
    ex::Request previous; uint32_t succeeded = 0, failed = 0;
    while (handled.size() < limit && GetTickCount64() - started < 300000 && GetFileAttributesW((directory + L"\\stop").c_str()) == INVALID_FILE_ATTRIBUTES) {
        WIN32_FIND_DATAW found{}; HANDLE search = FindFirstFileW((directory + L"\\request-*.bin").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) throw std::runtime_error("worker directory search failed");
            Sleep(25); continue;
        }
        std::vector<std::wstring> names;
        do {
            const std::wstring name = found.cFileName;
            if (name.size() <= 96 && name.find_first_not_of(L"request-0123456789abcdef.bin") == std::wstring::npos &&
                !(found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && !handled.count(name)) names.push_back(name);
        } while (FindNextFileW(search, &found));
        const auto search_error = GetLastError(); FindClose(search);
        if (search_error != ERROR_NO_MORE_FILES) throw std::runtime_error("worker directory enumeration failed");
        for (const auto &name : names) {
            if (handled.size() >= limit) break;
            handled.insert(name); ex::Header header{}; bool identified = false;
            try {
                std::vector<uint8_t> bytes;
                { io::File input(directory + L"\\" + name, false); const auto size = input.size();
                  ex::require(size >= sizeof(header) && size <= ex::max_packet_bytes); bytes.resize(size_t(size)); input.read(bytes.data(), DWORD(bytes.size())); }
                std::memcpy(&header, bytes.data(), sizeof(header));
                ex::require(ex::request_valid(header, bytes.size()) && packet_name(header, false) == name); identified = true;
                const auto request = ex::decode(bytes); const auto &h = request.header;
                const auto &old = previous.header;
                if (!executor || old.width != h.width || old.height != h.height || old.vertices != h.vertices || old.format != h.format || old.flags != h.flags ||
                    previous.vertex.bytecode != request.vertex.bytecode || previous.pixel.bytecode != request.pixel.bytecode) {
                    uint32_t batch = 4096; while (uint64_t(h.width) * h.height % batch) batch /= 2;
                    auto candidate = std::make_unique<nr::raster::UniformDrawExecutor>(request.vertex, request.pixel, h.vertices, h.width, h.height,
                        "NPU", (h.flags & 1) != 0, h.format == 87, uint8_t((h.flags >> 8) & 15), batch,
                        static_cast<nr::raster::CullMode>((h.flags>>5)&3),(h.flags&128)!=0);
                    executor = std::move(candidate);
                    previous = {}; previous.header = h; previous.vertex = request.vertex; previous.pixel = request.pixel;
                }
                const auto result = executor->execute(request.inputs, request.vertex_constants, request.pixel_constants, request.viewport, request.background, request.scissor);
                publish(directory + L"\\" + packet_name(h, true), ex::response(h, result)); ++succeeded;
                std::printf("{\"client_pid\":%u,\"sequence\":%u,\"execution_device\":\"%s\",\"width\":%u,\"height\":%u,\"provider_requests\":%llu,\"draw_ms\":%.3f,\"passed\":true,\"displayed_frames\":0}\n",
                    h.client_pid, h.sequence, result.execution_device.c_str(), h.width, h.height,
                    static_cast<unsigned long long>(result.provider_requests), result.timings.total_ms); std::fflush(stdout);
            } catch (const std::exception &error) {
                ++failed; executor.reset(); std::fprintf(stderr, "indexed NPU request failed: %.32000s\n", error.what()); std::fflush(stderr);
                if (identified) {
                    auto h = header; h.kind = 3; h.device = 0; h.body_bytes = 0; ex::Writer writer; writer.value(h);
                    publish(directory + L"\\" + packet_name(h, true), writer.finish());
                } else throw;
            }
        }
        Sleep(25);
    }
    std::printf("{\"worker_finished\":true,\"succeeded\":%u,\"failed\":%u,\"desktop_offload_tested\":false}\n", succeeded, failed);
    return failed ? 1 : 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
