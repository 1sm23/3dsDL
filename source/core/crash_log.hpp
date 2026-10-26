#pragma once

#include <cstddef>
#include <cstdint>

// What 3dsDL writes to sdmc:/3ds/3dsDL/crash.txt when it crashes, so a bug
// report can say where. Everything here works without allocating, since it
// runs after the crash, when the heap may be what broke.
// scripts/crash.py turns the addresses into functions and lines with the
// release's 3dsDL.elf.
namespace crashlog {

constexpr size_t ScreenCount = 6;
constexpr size_t ScreenLength = 64;
constexpr size_t StackCount = 40;

// The screens shown last, kept in fixed storage. Oldest go first.
class ScreenTrail {
public:
    void note(const char *id);
    // The screens, newest first, into out; returns how many.
    size_t newestFirst(const char *out[ScreenCount]) const;

private:
    char names_[ScreenCount][ScreenLength] = {};
    size_t next_ = 0;
    size_t count_ = 0;
};

struct Record {
    const char *version = "";
    bool cia = false;
    int64_t time = 0;        // the console's clock, seconds since 1970
    const char *what = "";   // "data abort", "terminate"
    const char *thread = ""; // "main", or the worker's name
    const char *screens[ScreenCount] = {};
    size_t screenCount = 0;
    uint32_t freeBytes = 0; // left in the application's memory region
    bool hasRegisters = false;
    uint32_t r[13] = {};
    uint32_t sp = 0;
    uint32_t lr = 0;
    uint32_t pc = 0; // as the kernel gave it, not adjusted
    uint32_t cpsr = 0;
    bool hasFault = false; // far and fsr, for aborts
    uint32_t far = 0;
    uint32_t fsr = 0;
    // Words on the stack that point into the code, nearest first: return
    // addresses among them make the backtrace.
    uint32_t stack[StackCount] = {};
    size_t stackCount = 0;
    // The last lines of log.txt, trace lines among them, oldest first.
    const char *const *log = nullptr;
    size_t logCount = 0;
};

// "data abort" for ERRF_ExceptionType's numbers.
const char *exceptionName(int type);

// Copies the words of stack that fall in [codeStart, codeEnd) to out, up
// to max of them; returns how many.
size_t codePointers(const uint32_t *stack, size_t words, uint32_t codeStart,
                    uint32_t codeEnd, uint32_t *out, size_t max);

// The crash.txt text: "key: value" lines under a "3dsDL crash" heading.
// Returns its length; it is cut short, still ending in a newline, when size
// is too small.
size_t format(const Record &record, char *out, size_t size);

} // namespace crashlog
