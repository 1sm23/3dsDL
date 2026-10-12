#include "../check.hpp"

#include "ntp.hpp"
#include "timezone.hpp"

#include <cstring>

namespace {

void reply(uint8_t packet[ntp::PacketSize], uint64_t origin, uint32_t seconds,
           uint32_t fraction, uint8_t stratum = 2, uint8_t leap = 0)
{
    std::memset(packet, 0, ntp::PacketSize);
    packet[0] = static_cast<uint8_t>((leap << 6) | (4 << 3) | 4);
    packet[1] = stratum;
    for (int index = 0; index < 8; ++index) {
        packet[24 + index] = static_cast<uint8_t>(origin >> (56 - 8 * index));
    }
    for (int index = 0; index < 4; ++index) {
        packet[40 + index] = static_cast<uint8_t>(seconds >> (24 - 8 * index));
        packet[44 + index] = static_cast<uint8_t>(fraction >> (24 - 8 * index));
    }
}

} // namespace

TEST("ntp: request layout")
{
    uint8_t packet[ntp::PacketSize];
    ntp::buildRequest(packet, 0x0102030405060708ULL);
    CHECK_EQ(static_cast<int>(packet[0]), 0x23); // version 4, client mode
    CHECK_EQ(static_cast<int>(packet[40]), 1);
    CHECK_EQ(static_cast<int>(packet[47]), 8);
}

TEST("ntp: response parsing and validation")
{
    uint8_t packet[ntp::PacketSize];
    int64_t unixMs = 0;
    std::string error;

    // 2026-10-12 06:00:00.25 UTC
    reply(packet, 42, static_cast<uint32_t>(1791784800ULL + 2208988800ULL),
          0x40000000);
    CHECK(ntp::parseResponse(packet, sizeof(packet), 42, unixMs, error));
    CHECK_EQ(unixMs, 1791784800LL * 1000 + 250);

    CHECK(!ntp::parseResponse(packet, sizeof(packet), 43, unixMs, error));
    CHECK(!ntp::parseResponse(packet, 47, 42, unixMs, error));

    reply(packet, 42, 3900000000u, 0, 0);
    CHECK(!ntp::parseResponse(packet, sizeof(packet), 42, unixMs, error));
    reply(packet, 42, 3900000000u, 0, 2, 3);
    CHECK(!ntp::parseResponse(packet, sizeof(packet), 42, unixMs, error));

    // After the 2036 rollover small second counts belong to the next era.
    reply(packet, 42, 100, 0);
    CHECK(ntp::parseResponse(packet, sizeof(packet), 42, unixMs, error));
    CHECK_EQ(unixMs, (4294967296LL + 100 - 2208988800LL) * 1000);
}

TEST("tz: offsets, zones and guesses")
{
    CHECK_EQ(tz::formatOffset(330), std::string("UTC+05:30"));
    CHECK_EQ(tz::formatOffset(-210), std::string("UTC-03:30"));
    CHECK_EQ(tz::formatOffset(0), std::string("UTC+00:00"));
    CHECK(tz::isValidOffset(345));
    CHECK(!tz::isValidOffset(7));
    CHECK(!tz::isValidOffset(900));
    CHECK(tz::zoneIndex(330) >= 0);
    CHECK_EQ(tz::zoneIndex(15), -1);

    const int64_t utc = 1791784800;
    int offset = 0;
    CHECK(tz::guessOffset(utc + 330 * 60 + 40, utc, offset));
    CHECK_EQ(offset, 330);
    CHECK(tz::guessOffset(utc - 300 * 60 - 90, utc, offset));
    CHECK_EQ(offset, -300);
    // A clock that is off by a year, or by an odd number of minutes, is
    // simply wrong rather than in another time zone.
    CHECK(!tz::guessOffset(utc - 365 * 86400, utc, offset));
    CHECK(!tz::guessOffset(utc + 37 * 60, utc, offset));
}

TEST("tz: drift descriptions")
{
    CHECK_EQ(tz::describeDrift(1000, 1030),
             std::string("correct to within a minute"));
    CHECK_EQ(tz::describeDrift(1000, 1000 + 600),
             std::string("10 minutes behind"));
    CHECK_EQ(tz::describeDrift(1000 + 5 * 3600 + 120, 1000),
             std::string("5 hours 2 minutes ahead"));
    CHECK_EQ(tz::describeDrift(0, 365 * 86400LL),
             std::string("about 365 days behind"));
}
