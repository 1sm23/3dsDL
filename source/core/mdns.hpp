#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Multicast DNS (RFC 6762) packets, just enough to answer "where is
// 3ds.local?" the way a Raspberry Pi answers for raspberrypi.local. Only
// IPv4 address records are answered. net/mdns_responder has the socket.
namespace mdns {

constexpr unsigned short Port = 5353;
constexpr const char *Group = "224.0.0.251";

// A question in a packet that asks for our name.
struct Asked {
    bool address = false;  // an A or ANY question for the name
    bool address6 = false; // an AAAA question, which the console has no
                           // answer for
    bool unicast = false;  // the asker set the "unicast reply" bit
    uint16_t id = 0;       // copied back to one-shot (legacy) askers
};

// Reads a query. True when it asks for name's IPv4 or IPv6 address; false
// for responses, other names and anything malformed. name is "3ds.local",
// in any case.
bool readQuery(const uint8_t *data, size_t size, const std::string &name,
               Asked &out);

// True when a response carries a record for name, which means another
// device already uses it.
bool claims(const uint8_t *data, size_t size, const std::string &name);

// A response with name's A record, when asked for, and an NSEC record
// saying there is no IPv6 address. Without that, a computer that asks for
// both waits about five seconds for an IPv6 answer before it connects.
// The NSEC goes with the answers when IPv6 was asked for, and as extra
// information otherwise. address is in network byte order.
//
// legacy answers a one-shot query from a port other than 5353: it repeats
// the question, keeps the query's ID, uses a short TTL and, for IPv6, just
// answers nothing, as RFC 6762 section 6.7 asks.
std::vector<uint8_t> answer(const std::string &name, uint32_t address,
                            const Asked &asked, bool legacy);

// A query that asks whether anyone already uses name.
std::vector<uint8_t> probe(const std::string &name);

} // namespace mdns
