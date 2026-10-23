#pragma once

#include "download_engine.hpp"
#include "file_ops.hpp"
#include "installer.hpp"
#include "zip.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Runs long jobs on a worker thread while the main thread keeps drawing the
// progress screen and reading the cancel button.
namespace tasks {

// step is shown after the title, e.g. "2 of 5" in a queue. direct says the
// data goes straight into an install (request.write) instead of a file.
net::Result download(const net::Request &request, bool keepAwake,
                     const std::string &step = "", bool direct = false);

zip::Result extract(const std::string &zipPath, const zip::Archive &archive,
                    const std::string &folder, zip::Existing existing,
                    bool keepAwake);

// Installs the CIA at path. name is shown while it runs.
installer::Result install(const std::string &path, uint64_t titleId,
                          const std::string &name, bool keepAwake,
                          const std::vector<uint16_t> &icon = {});

// Runs a copy, move or delete on the worker thread with a progress screen.
// total is what the observer's done counts towards: bytes, or files when
// countsFiles is true. folder is shown on the bottom screen.
fileops::Result fileOperation(
    const std::string &header, const std::string &title,
    const std::string &folder, uint64_t total, bool countsFiles,
    const std::function<fileops::Result(fileops::Observer &)> &work,
    bool keepAwake);

// Runs a short job that cannot be cancelled while showing a "please wait"
// screen.
void busy(const std::string &header, const std::string &title,
          const std::function<void()> &work);

// What a busy job shares with its "please wait" screen.
struct Busy {
    // Set when the user cancels (or 3dsDL is closing); the job should
    // notice soon and return early.
    std::atomic<bool> cancel {false};
    // done of total items, or bytes when bytes is set; a bar shows once
    // total is above zero.
    std::atomic<uint64_t> done {0};
    std::atomic<uint64_t> total {0};
    std::atomic<bool> bytes {false};
};

// The same, with a Busy the job reports progress through. When cancellable,
// B or the Cancel button sets busy.cancel. Returns false when the job was
// asked to stop.
bool busy(const std::string &header, const std::string &title,
          const std::function<void(Busy &)> &work, bool cancellable);

} // namespace tasks
