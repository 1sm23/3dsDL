#pragma once

#include "update.hpp"

#include <atomic>
#include <cstdint>
#include <string>

// Asks GitHub whether a newer 3dsDL is out. The check at startup runs on a
// background thread so the HOME screen never waits for it; screens read the
// latest answer with status().
namespace updater {

enum class State { NotChecked, Checking, UpToDate, Available, Failed };

struct Status {
    State state = State::NotChecked;
    // The newest release, when the check got that far.
    update::Release release;
    std::string message; // why the check failed
    bool certificateProblem = false;
};

// Remembers where the Homebrew Launcher started 3dsDL from (argv[0]).
void setLaunchPath(const char *path);
// True for the installed CIA, false for the .3dsx.
bool runningAsCia();
// The .3dsx an update replaces.
std::string homebrewPath();
// The release file this copy of 3dsDL updates from.
const char *assetName();

// Starts a check on a background thread unless one is already running.
void startCheck();
// Checks on the calling thread and returns the answer. If a background
// check is running, waits for that one instead of asking twice. Setting
// stop ends the check early, as a failed one.
Status checkNow(const std::atomic<bool> *stop = nullptr);
Status status();
// Goes up by one each time a check finishes, so a screen can notice a new
// answer without copying the status every frame.
uint32_t generation();
// Stops a running check; call before the app closes.
void shutdown();

// Downloads the release's checksum listing. Returns false with a reason.
// Setting stop ends it early.
bool fetchChecksums(const update::Release &release, std::string &listing,
                    std::string &error,
                    const std::atomic<bool> *stop = nullptr);

} // namespace updater
