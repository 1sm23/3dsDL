#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

// The lines of sdmc:/3ds/3dsDL/log.txt, which a user can attach to a bug
// report: what 3dsDL did, how long it took and what went wrong.
//
//   2026-10-26 14:03:22 I download.done host=example.com bytes=5242880 ms=812
//
// The console's clock, a level letter, an event name and key=value fields.
// A value with spaces or quotes is quoted, so a line splits on spaces
// outside quotes.
namespace eventlog {

// Trace lines are kept in memory for crash.txt and never written to the
// file; they say what was under way, step by step.
enum class Level { Trace, Info, Warn, Error };

// The file is moved aside to log.old.txt once it reaches this size, so the
// two together stay under twice it.
constexpr uint64_t RotateBytes = 256 * 1024;

struct Field {
    Field(const char *key, const std::string &value) : key(key), value(value) {}
    Field(const char *key, const char *value) : key(key), value(value) {}
    Field(const char *key, bool value) : key(key), value(value ? "yes" : "no") {}
    template <typename Number,
              std::enable_if_t<std::is_integral<Number>::value, int> = 0>
    Field(const char *key, Number value)
        : key(key), value(std::to_string(value))
    {
    }

    std::string key;
    std::string value;
};

using Fields = std::initializer_list<Field>;

char letter(Level level);
// "0xD8E007F7", for result codes.
std::string hex(uint32_t value);
// A value as it goes in a line: bare when it has no spaces, quotes or
// control characters, otherwise in double quotes with \" \\ \n escaped.
std::string quote(const std::string &value);
// "2026-10-26 14:03:22" into out, which holds at least 20 characters.
// Allocates nothing, for the crash handler.
void stamp(int64_t time, char out[20]);
// One line, without its newline. Fields with no value are left out.
std::string line(int64_t time, Level level, const std::string &event,
                 const std::vector<Field> &fields);

// How many times each event happened, for the line that ends a session.
class Tally {
public:
    void count(const std::string &event);
    // One field per event, by name.
    std::vector<Field> fields() const;

private:
    std::map<std::string, uint64_t> counts_;
};

constexpr size_t RecentCount = 20;
constexpr size_t RecentLength = 160;

// The newest lines, in fixed storage so the crash handler can read them
// without allocating. Longer lines are cut short.
class Recent {
public:
    void add(const char *line);
    // The lines, oldest first, into out; returns how many.
    size_t oldestFirst(const char *out[RecentCount]) const;

private:
    char lines_[RecentCount][RecentLength] = {};
    size_t next_ = 0;
    size_t count_ = 0;
};

} // namespace eventlog
