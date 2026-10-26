#include "crash_log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace crashlog {

namespace {

// Appends whole lines to a fixed buffer; a line that does not fit is left
// out, along with everything after it.
class Writer {
public:
    Writer(char *out, size_t size) : out_(out), size_(size)
    {
        if (size_ > 0) {
            out_[0] = '\0';
        }
    }

    __attribute__((format(printf, 2, 3))) void line(const char *format, ...)
    {
        if (full_ || size_ == 0) {
            return;
        }
        const size_t room = size_ - used_;
        va_list arguments;
        va_start(arguments, format);
        const int length = std::vsnprintf(out_ + used_, room, format, arguments);
        va_end(arguments);
        // One more for the newline.
        if (length < 0 || static_cast<size_t>(length) + 1 >= room) {
            out_[used_] = '\0';
            full_ = true;
            return;
        }
        used_ += static_cast<size_t>(length);
        out_[used_++] = '\n';
        out_[used_] = '\0';
    }

    size_t used() const { return used_; }

private:
    char *out_;
    size_t size_;
    size_t used_ = 0;
    bool full_ = false;
};

} // namespace

void ScreenTrail::note(const char *id)
{
    char *slot = names_[next_];
    std::strncpy(slot, id, ScreenLength - 1);
    slot[ScreenLength - 1] = '\0';
    next_ = (next_ + 1) % ScreenCount;
    if (count_ < ScreenCount) {
        ++count_;
    }
}

size_t ScreenTrail::newestFirst(const char *out[ScreenCount]) const
{
    for (size_t index = 0; index < count_; ++index) {
        out[index] = names_[(next_ + ScreenCount - 1 - index) % ScreenCount];
    }
    return count_;
}

const char *exceptionName(int type)
{
    switch (type) {
    case 0:
        return "prefetch abort";
    case 1:
        return "data abort";
    case 2:
        return "undefined instruction";
    case 3:
        return "VFP exception";
    default:
        return "exception";
    }
}

size_t codePointers(const uint32_t *stack, size_t words, uint32_t codeStart,
                    uint32_t codeEnd, uint32_t *out, size_t max)
{
    size_t found = 0;
    for (size_t index = 0; index < words && found < max; ++index) {
        if (stack[index] >= codeStart && stack[index] < codeEnd) {
            out[found++] = stack[index];
        }
    }
    return found;
}

size_t format(const Record &record, char *out, size_t size)
{
    Writer writer(out, size);
    writer.line("3dsDL crash");
    writer.line("version: %s", record.version);
    writer.line("build: %s", record.cia ? "CIA" : "3DSX");
    if (record.time > 0) {
        const time_t when = static_cast<time_t>(record.time);
        tm parts {};
        gmtime_r(&when, &parts);
        writer.line("time: %04d-%02d-%02d %02d:%02d:%02d", parts.tm_year + 1900,
                    parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min,
                    parts.tm_sec);
    }
    writer.line("what: %s", record.what);
    writer.line("thread: %s", record.thread);
    for (size_t index = 0; index < record.screenCount; ++index) {
        writer.line("screen: %s", record.screens[index]);
    }
    writer.line("free: %lu", static_cast<unsigned long>(record.freeBytes));
    if (record.hasRegisters) {
        writer.line("pc: %08lX", static_cast<unsigned long>(record.pc));
        writer.line("lr: %08lX", static_cast<unsigned long>(record.lr));
        writer.line("sp: %08lX", static_cast<unsigned long>(record.sp));
        writer.line("cpsr: %08lX", static_cast<unsigned long>(record.cpsr));
        if (record.hasFault) {
            writer.line("far: %08lX", static_cast<unsigned long>(record.far));
            writer.line("fsr: %08lX", static_cast<unsigned long>(record.fsr));
        }
        for (int index = 0; index < 13; ++index) {
            writer.line("r%d: %08lX", index,
                        static_cast<unsigned long>(record.r[index]));
        }
    }
    // Eight to a line, so the file stays readable in a bug report.
    for (size_t start = 0; start < record.stackCount; start += 8) {
        char words[8 * 9 + 1] = {};
        size_t used = 0;
        for (size_t index = start; index < record.stackCount && index < start + 8;
             ++index) {
            const int written = std::snprintf(
                words + used, sizeof(words) - used, used == 0 ? "%08lX" : " %08lX",
                static_cast<unsigned long>(record.stack[index]));
            if (written < 0 || static_cast<size_t>(written) >= sizeof(words) - used) {
                break;
            }
            used += static_cast<size_t>(written);
        }
        writer.line("stack: %s", words);
    }
    for (size_t index = 0; index < record.logCount; ++index) {
        writer.line("log: %s", record.log[index]);
    }
    return writer.used();
}

} // namespace crashlog
