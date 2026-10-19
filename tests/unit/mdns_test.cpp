#include "../check.hpp"

#include "mdns.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

bool contains(const std::vector<uint8_t> &packet,
              const std::vector<uint8_t> &part)
{
    return std::search(packet.begin(), packet.end(), part.begin(),
                       part.end()) != packet.end();
}

uint32_t address(const char *text)
{
    in_addr parsed {};
    inet_aton(text, &parsed);
    return parsed.s_addr;
}

} // namespace

TEST("mdns: reads questions for the console's name")
{
    std::vector<uint8_t> probe = mdns::probe("3ds.local");
    mdns::Asked asked;
    CHECK(mdns::readQuery(probe.data(), probe.size(), "3DS.LOCAL", asked));
    CHECK(asked.address);
    CHECK(asked.unicast);
    CHECK(!mdns::readQuery(probe.data(), probe.size(), "3ds-2.local", asked));
    // Answers are not questions, and probes claim nothing.
    CHECK(!mdns::claims(probe.data(), probe.size(), "3ds.local"));

    // What macOS sends: two questions, an AAAA one and then one naming
    // .local through a compression pointer.
    const std::vector<uint8_t> query = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 'a',  'b',  'c',  'd',  0x05, 'l',  'o',  'c',  'a',  'l',  0x00,
        0x00, 0x1c, 0x00, 0x01,                         // abcd.local AAAA
        0x03, '3',  'd',  's',  0xc0, 0x11,             // 3ds + pointer
        0x00, 0x01, 0x00, 0x01};                        // A, IN
    CHECK(mdns::readQuery(query.data(), query.size(), "3ds.local", asked));
    CHECK(!asked.unicast);
    // The AAAA question is for IPv6 only, which gets an answer that there
    // is none.
    CHECK(mdns::readQuery(query.data(), query.size(), "abcd.local", asked));
    CHECK(asked.address6 && !asked.address);

    // A pointer to itself must not loop forever.
    const std::vector<uint8_t> loop = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                       0xc0, 0x0c, 0, 1, 0, 1};
    CHECK(!mdns::readQuery(loop.data(), loop.size(), "3ds.local", asked));
    CHECK(!mdns::readQuery(loop.data(), 5, "3ds.local", asked));
}

TEST("mdns: answers, for the group and for one-shot askers")
{
    mdns::Asked asked;
    asked.id = 0x1234;
    asked.address = true;
    const std::vector<uint8_t> multicast =
        mdns::answer("3ds.local", address("192.168.1.20"), asked, false);
    CHECK(mdns::claims(multicast.data(), multicast.size(), "3ds.local"));
    CHECK(!mdns::claims(multicast.data(), multicast.size(), "3ds-2.local"));
    CHECK_EQ(multicast[0], 0); // the ID is 0 on the group
    CHECK_EQ(multicast[7], 1);  // one answer: the address
    CHECK_EQ(multicast[11], 1); // and the NSEC as extra information
    CHECK(contains(multicast, {0, 1, 0x80, 1, 0, 0, 0, 120, 0, 4, 192, 168, 1, 20}));
    // NSEC, cache-flush, TTL 120, then the bitmap that lists only A.
    CHECK(contains(multicast, {0, 47, 0x80, 1, 0, 0, 0, 120}));
    CHECK(contains(multicast, {0, 0, 1, 0x40}));

    const std::vector<uint8_t> legacy =
        mdns::answer("3ds.local", address("10.0.0.7"), asked, true);
    CHECK(!contains(legacy, {0, 47}));
    CHECK_EQ(legacy[0], 0x12);
    CHECK_EQ(legacy[1], 0x34);
    CHECK_EQ(legacy[5], 1); // the question comes back
    CHECK(mdns::claims(legacy.data(), legacy.size(), "3ds.local"));
    CHECK(std::vector<uint8_t>(legacy.end() - 4, legacy.end()) ==
          std::vector<uint8_t>({10, 0, 0, 7}));
}

TEST("mdns: IPv6 questions get an answer that there is none")
{
    // What getaddrinfo on macOS sends next to its A question.
    std::vector<uint8_t> query = mdns::probe("3ds.local");
    query[query.size() - 4] = 0;
    query[query.size() - 3] = 28; // AAAA
    query[query.size() - 2] = 0;  // no unicast bit
    mdns::Asked asked;
    CHECK(mdns::readQuery(query.data(), query.size(), "3ds.local", asked));
    CHECK(asked.address6);
    CHECK(!asked.address);

    const std::vector<uint8_t> reply =
        mdns::answer("3ds.local", address("192.168.1.20"), asked, false);
    CHECK_EQ(reply[7], 1);  // the NSEC is the answer
    CHECK_EQ(reply[11], 0);
    CHECK(contains(reply, {0, 47, 0x80, 1}));
    CHECK(!contains(reply, {192, 168, 1, 20}));

    // A one-shot resolver just hears that there is nothing.
    asked.id = 7;
    const std::vector<uint8_t> legacy =
        mdns::answer("3ds.local", address("192.168.1.20"), asked, true);
    CHECK_EQ(legacy[5], 1); // the question
    CHECK_EQ(legacy[7], 0); // no answers
    CHECK(contains(legacy, {0, 28, 0, 1}));
}
