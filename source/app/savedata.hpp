#pragma once

#include "titles.hpp"

#include <3ds.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Backs up, restores and opens a title's save data and extra data, the way
// Checkpoint does, into Checkpoint's folders (see core/saves.hpp), so its
// backups and 3dsDL's are the same thing.
namespace savedata {

enum class Kind { Save, ExtData };

struct Target {
    Kind kind = Kind::Save;
    uint64_t titleId = 0;
    FS_MediaType media = MEDIATYPE_SD; // where the title is; extra data is
                                       // always on the SD card
    uint32_t extDataId = 0;
    std::string name;   // for the user
    std::string folder; // this title's Checkpoint folder, ending in '/'
};

Target saveOf(const titles::Title &title, FS_MediaType media);
Target extDataOf(const titles::Title &title);
// Extra data whose game is not installed, named from its own icon.
Target extDataOf(uint64_t extDataId, const std::string &englishShortName,
                 const std::string &name);

// The title has data of this kind (its archive opens).
bool exists(const Target &target);
// Why Target cannot be handled, or "" when it can. DSiWare saves need a
// different route that 3dsDL does not take.
std::string unsupportedReason(const titles::Title &title);

// Backup folder names in target.folder, newest first.
std::vector<std::string> backups(const Target &target);

struct Progress {
    std::atomic<uint32_t> files {0};
    std::atomic<uint64_t> bytes {0};
    std::atomic<bool> cancel {false};
};

struct Result {
    bool ok = false;
    uint32_t files = 0;
    uint32_t skipped = 0; // extra data the system held open
    std::string message;  // why it failed
};

// Copies the data into target.folder + name + '/'. An existing backup of
// that name is replaced; a failed one is removed again.
Result backup(const Target &target, const std::string &name, Progress &progress);

// Replaces the data with the backup in target.folder + name + '/'. The
// backup is checked before anything on the console is touched.
Result restore(const Target &target, const std::string &name, Progress &progress);

// Opens the data as a read-only device ("save:/" style) for browsing.
bool mount(const Target &target, const std::string &device, std::string &error);
void unmount(const std::string &device);

} // namespace savedata
