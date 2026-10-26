#include "../check.hpp"

#include "event_log.hpp"

#include <string>

TEST("event log: a line is time, level, event and fields")
{
    const std::string line = eventlog::line(
        1792765385, eventlog::Level::Info, "download.done",
        {{"host", "example.com"}, {"bytes", uint64_t(5242880)}, {"resumed", false},
         {"message", ""}});
    CHECK_EQ(line, std::string("2026-10-23 14:23:05 I download.done "
                               "host=example.com bytes=5242880 resumed=no"));
    CHECK_EQ(eventlog::line(0, eventlog::Level::Error, "start", {}),
             std::string("1970-01-01 00:00:00 E start"));
}

TEST("event log: values with spaces or quotes are quoted")
{
    CHECK_EQ(eventlog::quote("sdmc:/3ds/3dsDL/"), std::string("sdmc:/3ds/3dsDL/"));
    CHECK_EQ(eventlog::quote(""), std::string("\"\""));
    CHECK_EQ(eventlog::quote("New 2DS XL"), std::string("\"New 2DS XL\""));
    CHECK_EQ(eventlog::quote("say \"hi\"\\"), std::string("\"say \\\"hi\\\"\\\\\""));
    CHECK_EQ(eventlog::quote("two\nlines\tand"),
             std::string("\"two\\nlines and\""));
    CHECK_EQ(eventlog::quote("Pokémon"), std::string("Pokémon"));
}

TEST("event log: result codes are hex")
{
    CHECK_EQ(eventlog::hex(0xD8E007F7u), std::string("0xD8E007F7"));
    CHECK_EQ(eventlog::hex(0), std::string("0x00000000"));
}

TEST("event log: the tally counts each event, by name")
{
    eventlog::Tally tally;
    tally.count("screen");
    tally.count("download.done");
    tally.count("screen");
    const std::vector<eventlog::Field> fields = tally.fields();
    CHECK_EQ(fields.size(), size_t(2));
    CHECK_EQ(fields[0].key + "=" + fields[0].value, std::string("download.done=1"));
    CHECK_EQ(fields[1].key + "=" + fields[1].value, std::string("screen=2"));
}

TEST("event log: recent lines keep the newest, oldest first")
{
    eventlog::Recent recent;
    const char *lines[eventlog::RecentCount] = {};
    CHECK_EQ(recent.oldestFirst(lines), size_t(0));
    recent.add("one");
    recent.add("two");
    CHECK_EQ(recent.oldestFirst(lines), size_t(2));
    CHECK_EQ(std::string(lines[0]), std::string("one"));
    CHECK_EQ(std::string(lines[1]), std::string("two"));
    for (size_t index = 0; index < eventlog::RecentCount + 3; ++index) {
        recent.add(("line " + std::to_string(index)).c_str());
    }
    CHECK_EQ(recent.oldestFirst(lines), eventlog::RecentCount);
    CHECK_EQ(std::string(lines[0]), std::string("line 3"));
    CHECK_EQ(std::string(lines[eventlog::RecentCount - 1]),
             "line " + std::to_string(eventlog::RecentCount + 2));

    recent.add(std::string(400, 'x').c_str());
    recent.oldestFirst(lines);
    CHECK_EQ(std::string(lines[eventlog::RecentCount - 1]).size(),
             eventlog::RecentLength - 1);
}
