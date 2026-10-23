#pragma once

#include <string>

// When 3dsDL crashes, writes what it was doing and where to crash.txt
// (crashlog), then hands the crash to the system's error screen as before.
namespace crash {

constexpr const char *LogPath = "sdmc:/3ds/3dsDL/crash.txt";

// Call first thing in main. The handler goes on the main thread: the kernel
// sends an exception on a thread without its own handler to the main
// thread's, which Luma3DS enables on retail consoles. Exceptions that end in
// std::terminate (out of memory among them) are written too.
void install(bool cia);

// A screen has opened, for the trail of screens in the log.
void noteScreen(const std::string &id);

} // namespace crash
