#include "../check.hpp"

#include "mdns.hpp"

#include <arpa/inet.h>

#include <cstdint>
#include <vector>

namespace {

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

    // What macOS sends: two questions, the second naming .local through a
    // compression pointer, and an AAAA question that is not answered.
    const std::vector<uint8_t> query = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 'a',  'b',  'c',  'd',  0x05, 'l',  'o',  'c',  'a',  'l',  0x00,
        0x00, 0x1c, 0x00, 0x01,                         // abcd.local AAAA
        0x03, '3',  'd',  's',  0xc0, 0x11,             // 3ds + pointer
        0x00, 0x01, 0x00, 0x01};                        // A, IN
    CHECK(mdns::readQuery(query.data(), query.size(), "3ds.local", asked));
    CHECK(!asked.unicast);
    CHECK(!mdns::readQuery(query.data(), query.size(), "abcd.local", asked));

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
    const std::vector<uint8_t> multicast =
        mdns::answer("3ds.local", address("192.168.1.20"), asked, false);
    CHECK(mdns::claims(multicast.data(), multicast.size(), "3ds.local"));
    CHECK(!mdns::claims(multicast.data(), multicast.size(), "3ds-2.local"));
    CHECK_EQ(multicast[0], 0); // the ID is 0 on the group
    const std::vector<uint8_t> tail(multicast.end() - 4, multicast.end());
    CHECK(tail == std::vector<uint8_t>({192, 168, 1, 20}));

    const std::vector<uint8_t> legacy =
        mdns::answer("3ds.local", address("10.0.0.7"), asked, true);
    CHECK_EQ(legacy[0], 0x12);
    CHECK_EQ(legacy[1], 0x34);
    CHECK_EQ(legacy[5], 1); // the question comes back
    CHECK(mdns::claims(legacy.data(), legacy.size(), "3ds.local"));
    CHECK(std::vector<uint8_t>(legacy.end() - 4, legacy.end()) ==
          std::vector<uint8_t>({10, 0, 0, 7}));
}
