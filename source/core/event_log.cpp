#include "event_log.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace eventlog {

char letter(Level level)
{
    switch (level) {
    case Level::Trace:
        return 'T';
    case Level::Info:
        return 'I';
    case Level::Warn:
        return 'W';
    case Level::Error:
        return 'E';
    }
    return '?';
}

std::string hex(uint32_t value)
{
    char text[11];
    std::snprintf(text, sizeof(text), "0x%08lX",
                  static_cast<unsigned long>(value));
    return text;
}

std::string quote(const std::string &value)
{
    bool bare = !value.empty();
    for (const char c : value) {
        if (c == ' ' || c == '"' || c == '\\' ||
            static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
            bare = false;
            break;
        }
    }
    if (bare) {
        return value;
    }
    std::string quoted = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            quoted += '\\';
            quoted += c;
        } else if (c == '\n') {
            quoted += "\\n";
        } else if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
            quoted += ' ';
        } else {
            quoted += c;
        }
    }
    return quoted + "\"";
}

void stamp(int64_t time, char out[20])
{
    const time_t when = static_cast<time_t>(time);
    tm parts {};
    gmtime_r(&when, &parts);
    const auto part = [](int value, unsigned limit) {
        return static_cast<unsigned>(value) % limit;
    };
    std::snprintf(out, 20, "%04u-%02u-%02u %02u:%02u:%02u",
                  part(parts.tm_year + 1900, 10000), part(parts.tm_mon + 1, 100),
                  part(parts.tm_mday, 100), part(parts.tm_hour, 100),
                  part(parts.tm_min, 100), part(parts.tm_sec, 100));
}

std::string line(int64_t time, Level level, const std::string &event,
                 const std::vector<Field> &fields)
{
    char when[20];
    stamp(time, when);
    std::string text = when;
    text += ' ';
    text += letter(level);
    text += ' ';
    text += event;
    for (const Field &field : fields) {
        if (field.value.empty()) {
            continue;
        }
        text += ' ';
        text += field.key;
        text += '=';
        text += quote(field.value);
    }
    return text;
}

void Tally::count(const std::string &event)
{
    ++counts_[event];
}

std::vector<Field> Tally::fields() const
{
    std::vector<Field> out;
    for (const auto &entry : counts_) {
        out.emplace_back(entry.first.c_str(), entry.second);
    }
    return out;
}

void Recent::add(const char *line)
{
    char *slot = lines_[next_];
    std::strncpy(slot, line, RecentLength - 1);
    slot[RecentLength - 1] = '\0';
    next_ = (next_ + 1) % RecentCount;
    if (count_ < RecentCount) {
        ++count_;
    }
}

size_t Recent::oldestFirst(const char *out[RecentCount]) const
{
    for (size_t index = 0; index < count_; ++index) {
        out[index] = lines_[(next_ + RecentCount - count_ + index) % RecentCount];
    }
    return count_;
}

} // namespace eventlog
