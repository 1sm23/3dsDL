#pragma once

#include "event_log.hpp"

#include <string>

// Writes sdmc:/3ds/3dsDL/log.txt (eventlog), for bug reports and for seeing
// how 3dsDL is used: what happened, how long it took and what went wrong.
// It stays on the SD card; nothing is sent anywhere. Any thread may log.
//
// Events are named area.what ("download.done", "titles.failed") and say
// facts as fields. Log what a job did once, when it ends, never per file or
// per frame; log hosts and file names, never whole URLs, which can carry
// keys. Trace is for steps worth seeing in crash.txt but not worth keeping.
namespace logging {

constexpr const char *Path = "sdmc:/3ds/3dsDL/log.txt";
constexpr const char *OldPath = "sdmc:/3ds/3dsDL/log.old.txt";

// Opens the log and writes the session's first line: version, console,
// memory and SD card. Call once, early in main; lines before it are lost.
void start(bool cia);
// Writes the session's last line, with how long it lasted and how often
// each event happened, and closes the log.
void finish();

void log(eventlog::Level level, const std::string &event,
         eventlog::Fields fields = {});
void trace(const std::string &event, eventlog::Fields fields = {});
void info(const std::string &event, eventlog::Fields fields = {});
void warn(const std::string &event, eventlog::Fields fields = {});
void error(const std::string &event, eventlog::Fields fields = {});

// True once after each write to the SD card, for App::sdChanged.
bool takeWritten();

// For the crash handler, which cannot allocate or wait for a lock: the last
// lines, oldest first, and a last line saying 3dsDL crashed.
size_t recent(const char *out[eventlog::RecentCount]);
void crashed(const char *what, const char *thread);

} // namespace logging
