#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A small SNTP (RFC 4330) client for reading the current UTC time. It uses
// plain UDP, so it works even when the console clock is too wrong for HTTPS.
namespace ntp {

constexpr size_t PacketSize = 48;

struct Result {
    bool ok = false;
    int64_t unixMilliseconds = 0; // UTC
    std::string server;
    std::string error;
};

// Fills a client request whose transmit timestamp is nonce, so the answer can
// be matched to it.
void buildRequest(uint8_t packet[PacketSize], uint64_t nonce);

// Checks that a reply answers the request with this nonce and comes from a
// synchronised server, and returns its transmit time.
bool parseResponse(const uint8_t *packet, size_t size, uint64_t nonce,
                   int64_t &unixMilliseconds, std::string &error);

Result query(const std::string &host, uint16_t port = 123,
             int timeoutMilliseconds = 2500);

// Tries each host in turn and returns the first answer.
Result queryAny(const std::vector<std::string> &hosts, uint16_t port = 123,
                int timeoutMilliseconds = 2500);

// Public NTP services used by the app.
const std::vector<std::string> &defaultServers();

} // namespace ntp
