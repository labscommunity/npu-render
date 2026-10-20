#pragma once
// Include before Windows headers. Only the relay and worker use sockets.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "indexed_pipe.hpp"
#include "lab_exchange_io.hpp"
namespace nr::registered_lab::indexed_stream {
struct Winsock {
    Winsock() { WSADATA data{}; if (WSAStartup(MAKEWORD(2,2), &data)) throw std::runtime_error("Winsock startup failed"); }
    ~Winsock() { WSACleanup(); }
};
struct Socket {
    SOCKET value = INVALID_SOCKET;
    explicit Socket(SOCKET s) : value(s) { if (s == INVALID_SOCKET) throw std::runtime_error("indexed socket unavailable"); }
    ~Socket() { closesocket(value); }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
};
inline void nonblocking(SOCKET socket) {
    u_long enabled = 1; if (ioctlsocket(socket, FIONBIO, &enabled)) throw std::runtime_error("indexed socket mode failed");
    const BOOL no_delay = TRUE;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&no_delay), sizeof(no_delay))) throw std::runtime_error("indexed socket latency option failed");
}
inline void wait_socket(SOCKET socket, short events, ULONGLONG deadline) {
    WSAPOLLFD poll{socket, events, 0};
    if (WSAPoll(&poll, 1, int(remaining(deadline))) <= 0 || !(poll.revents & events)) throw std::runtime_error("indexed socket closed or timed out");
}
inline void socket_transfer(SOCKET socket, void *data, uint32_t bytes, bool write, ULONGLONG deadline) {
    auto p = static_cast<char *>(data); uint32_t cursor = 0;
    while (cursor < bytes) {
        wait_socket(socket, write ? POLLWRNORM : POLLRDNORM, deadline);
        const auto count = int((std::min)(bytes - cursor, uint32_t(64 * 1024)));
        const auto actual = write ? send(socket, p + cursor, count, 0) : recv(socket, p + cursor, count, 0);
        if (actual == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (actual <= 0) throw std::runtime_error("indexed socket truncated transfer"); cursor += uint32_t(actual);
    }
}
inline std::vector<uint8_t> socket_read(SOCKET socket, ULONGLONG deadline) {
    uint32_t size = 0; socket_transfer(socket, &size, 4, false, deadline);
    indexed_exchange::require(size >= sizeof(indexed_exchange::Header) && size <= indexed_exchange::max_packet_bytes);
    std::vector<uint8_t> bytes(size); socket_transfer(socket, bytes.data(), size, false, deadline); return bytes;
}
inline void socket_write(SOCKET socket, const std::vector<uint8_t> &bytes, ULONGLONG deadline) {
    indexed_exchange::require(bytes.size() >= sizeof(indexed_exchange::Header) && bytes.size() <= indexed_exchange::max_packet_bytes);
    auto size = uint32_t(bytes.size()); socket_transfer(socket, &size, 4, true, deadline);
    socket_transfer(socket, const_cast<uint8_t *>(bytes.data()), size, true, deadline);
}
inline std::array<uint8_t,32> secret(const std::wstring &path) {
    exchange::File file(path, false); indexed_exchange::require(file.size() == 32);
    std::array<uint8_t,32> value{}; file.read(value.data(), DWORD(value.size())); return value;
}
inline void authenticate(SOCKET socket, std::array<uint8_t,32> token, bool server, ULONGLONG deadline) {
    // This link is carried by a controller-owned SSH tunnel, never plaintext LAN.
    std::array<uint8_t,32> received{};
    if (server) {
        socket_transfer(socket, received.data(), 32, false, deadline);
        uint8_t different = 0; for (size_t i = 0; i < 32; ++i) different |= received[i] ^ token[i];
        if (different) throw std::runtime_error("indexed worker authentication rejected");
        socket_transfer(socket, token.data(), 32, true, deadline);
    } else {
        socket_transfer(socket, token.data(), 32, true, deadline); socket_transfer(socket, received.data(), 32, false, deadline);
        uint8_t different = 0; for (size_t i = 0; i < 32; ++i) different |= received[i] ^ token[i];
        if (different) throw std::runtime_error("indexed relay authentication rejected");
    }
    SecureZeroMemory(token.data(), token.size()); SecureZeroMemory(received.data(), received.size());
}
}
