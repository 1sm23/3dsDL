#include "logging.hpp"

#include "crash.hpp"
#include "feedback.hpp"
#include "fs_util.hpp"
#include "platform.hpp"

#include <3ds.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <malloc.h>
#include <vector>

namespace logging {

namespace {

LightLock lock;
std::atomic<bool> started {false};
std::atomic<bool> written {false};
// Guarded by lock, except in crashed().
Handle file = 0;
u64 offset = 0;
eventlog::Tally tally;
eventlog::Recent lines;
u64 startedAt = 0;
char crashLine[256];

// Straight through FS, so the crash handler can use the same handle.
bool open()
{
    file = 0;
    offset = 0;
    if (R_FAILED(FSUSER_OpenFileDirectly(
            &file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
            fsMakePath(PATH_ASCII, Path + 5 /* without "sdmc:" */),
            FS_OPEN_WRITE | FS_OPEN_CREATE, 0))) {
        file = 0;
        return false;
    }
    if (R_FAILED(FSFILE_GetSize(file, &offset))) {
        FSFILE_Close(file);
        file = 0;
        return false;
    }
    return true;
}

void close()
{
    if (file != 0) {
        FSFILE_Close(file);
        file = 0;
    }
}

// log.txt becomes log.old.txt, replacing the one before.
void rotate()
{
    close();
    std::remove(OldPath);
    std::rename(Path, OldPath);
    open();
}

void write(eventlog::Level level, const std::string &event,
           const std::vector<eventlog::Field> &fields)
{
    if (!started.load()) {
        return;
    }
    std::string text = eventlog::line(std::time(nullptr), level, event, fields);
    LightLock_Lock(&lock);
    lines.add(text.c_str());
    if (level != eventlog::Level::Trace) {
        if (event.compare(0, 8, "session.") != 0) {
            tally.count(event);
        }
        if (file != 0 && offset >= eventlog::RotateBytes) {
            rotate();
        }
        if (file != 0) {
            text += '\n';
            u32 done = 0;
            // Flushed, so the lines before a freeze survive turning the
            // console off.
            if (R_SUCCEEDED(FSFILE_Write(file, &done, offset, text.data(),
                                         static_cast<u32>(text.size()),
                                         FS_WRITE_FLUSH))) {
                offset += done;
            }
            written = true;
        }
    }
    LightLock_Unlock(&lock);
}

// The heap's size, what is in use, the most it has held, and what is left
// for graphics and sound.
void addMemory(std::vector<eventlog::Field> &fields)
{
    const struct mallinfo heap = mallinfo();
    fields.emplace_back("heap", envGetHeapSize());
    fields.emplace_back("heapUsed", heap.uordblks);
    fields.emplace_back("heapPeak", heap.arena);
    fields.emplace_back("linearFree", linearSpaceFree());
}

} // namespace

void start(bool cia)
{
    LightLock_Init(&lock);
    fs::makeDirectories("sdmc:/3ds/3dsDL/");
    open();
    startedAt = osGetTime();
    started = true;
    std::vector<eventlog::Field> fields = {
        {"version", APP_VERSION},
        {"build", cia ? "CIA" : "3DSX"},
        {"model", feedback::modelName(platform::systemModel())},
        {"system", platform::systemVersion()},
        {"sdFree", platform::sdFreeBytes()},
        {"sdTotal", platform::sdTotalBytes()},
        {"crashLog", fs::fileExists(crash::LogPath)}};
    addMemory(fields);
    write(eventlog::Level::Info, "session.start", fields);
}

void finish()
{
    if (!started.load()) {
        return;
    }
    LightLock_Lock(&lock);
    std::vector<eventlog::Field> fields = tally.fields();
    LightLock_Unlock(&lock);
    fields.insert(fields.begin(),
                  eventlog::Field("seconds", (osGetTime() - startedAt) / 1000));
    addMemory(fields);
    write(eventlog::Level::Info, "session.end", fields);
    started = false;
    LightLock_Lock(&lock);
    close();
    LightLock_Unlock(&lock);
}

void log(eventlog::Level level, const std::string &event,
         eventlog::Fields fields)
{
    write(level, event, fields);
}

void trace(const std::string &event, eventlog::Fields fields)
{
    write(eventlog::Level::Trace, event, fields);
}

void info(const std::string &event, eventlog::Fields fields)
{
    write(eventlog::Level::Info, event, fields);
}

void warn(const std::string &event, eventlog::Fields fields)
{
    write(eventlog::Level::Warn, event, fields);
}

void error(const std::string &event, eventlog::Fields fields)
{
    write(eventlog::Level::Error, event, fields);
}

bool takeWritten()
{
    return written.exchange(false);
}

size_t recent(const char *out[eventlog::RecentCount])
{
    return lines.oldestFirst(out);
}

void crashed(const char *what, const char *thread)
{
    if (!started.load() || file == 0) {
        return;
    }
    char when[20];
    eventlog::stamp(std::time(nullptr), when);
    const int length =
        std::snprintf(crashLine, sizeof(crashLine),
                      "%s E crash what=\"%s\" thread=\"%s\"\n", when, what, thread);
    if (length <= 0) {
        return;
    }
    u32 done = 0;
    FSFILE_Write(file, &done, offset, crashLine,
                 static_cast<u32>(std::min<int>(length, sizeof(crashLine) - 1)),
                 FS_WRITE_FLUSH);
}

} // namespace logging
