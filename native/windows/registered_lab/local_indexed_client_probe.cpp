#include "local_indexed_client.hpp"
#include "lab_exchange_io.hpp"
#include <cstdio>
#include <cwchar>
#include <cstring>

int wmain(int argc, wchar_t **argv) try {
    if (argc != 7) throw std::invalid_argument("expected <pipe> <owned worker pid> <request> <new reply> <timeout ms> <success|failure|cancel>");
    wchar_t *end = nullptr;
    const auto pid = std::wcstoul(argv[2], &end, 10);
    nr::registered_lab::indexed_exchange::require(end != argv[2] && !*end && pid);
    end = nullptr; const auto timeout = std::wcstoul(argv[5], &end, 10);
    nr::registered_lab::indexed_exchange::require(end != argv[5] && !*end && timeout && timeout <= 90000);
    const bool expect_failure = !std::wcscmp(argv[6], L"failure"), cancel = !std::wcscmp(argv[6], L"cancel");
    nr::registered_lab::indexed_exchange::require(expect_failure || cancel || !std::wcscmp(argv[6], L"success"));
    namespace ex = nr::registered_lab::indexed_exchange;
    namespace stream = nr::registered_lab::indexed_stream;
    nr::registered_lab::exchange::File input(argv[3], false);
    const auto size = input.size(); ex::require(size >= sizeof(ex::Header) && size <= ex::max_packet_bytes);
    std::vector<uint8_t> bytes(size_t(size), 0); input.read(bytes.data(), DWORD(bytes.size()));
    auto header = ex::Reader(bytes).value<ex::Header>(); header.client_pid = GetCurrentProcessId();
    std::memcpy(bytes.data(), &header, sizeof(header));
    stream::Handle worker(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    nr::registered_lab::LocalIndexedClient client(argv[1], worker.value, timeout);
    stream::Handle stop(CreateEventW(nullptr, TRUE, cancel, nullptr));
    std::vector<uint8_t> reply;
    try { reply = client.exchange(bytes, stop.value); }
    catch (const std::exception &e) {
        if (!expect_failure && !cancel) throw;
        bool poisoned = false;
        try { (void)client.exchange(bytes); }
        catch (const std::runtime_error &again) { poisoned = !std::strcmp(again.what(), "local NPU client previously failed"); }
        ex::require(poisoned);
        std::printf("{\"expected_failure\":true,\"client_poisoned\":true,\"client_pid\":%lu,\"message\":\"%s\"}\n", GetCurrentProcessId(), e.what());
        return 0;
    }
    ex::require(!expect_failure && !cancel);
    nr::registered_lab::exchange::File output(argv[4], true);
    output.write(reply.data(), DWORD(reply.size())); output.flush();
    std::printf("{\"native_client_success\":true,\"client_pid\":%lu,\"reply_bytes\":%zu,\"physical_driver_installed\":false}\n", GetCurrentProcessId(), reply.size());
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
