#pragma once

#include "download_engine.hpp"
#include "file_ops.hpp"
#include "installer.hpp"
#include "zip.hpp"

#include <functional>
#include <string>

// Runs long jobs on a worker thread while the main thread keeps drawing the
// progress screen and reading the cancel button.
namespace tasks {

net::Result download(const net::Request &request, bool keepAwake);

zip::Result extract(const std::string &zipPath, const zip::Archive &archive,
                    const std::string &folder, zip::Existing existing,
                    bool keepAwake);

// Installs the CIA at path. name is shown while it runs.
installer::Result install(const std::string &path, uint64_t titleId,
                          const std::string &name, bool keepAwake);

// Runs a copy, move or delete on the worker thread with a progress screen.
// total is what the observer's done counts towards: bytes, or files when
// countsFiles is true. folder is shown on the bottom screen.
fileops::Result fileOperation(
    const std::string &header, const std::string &title,
    const std::string &folder, uint64_t total, bool countsFiles,
    const std::function<fileops::Result(fileops::Observer &)> &work,
    bool keepAwake);

// Runs a short job that cannot be cancelled, such as asking a time server,
// while showing a "please wait" screen.
void busy(const std::string &header, const std::string &title,
          const std::function<void()> &work);

} // namespace tasks
