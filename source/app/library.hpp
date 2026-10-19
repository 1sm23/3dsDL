#pragma once

#include "titles.hpp"

#include <3ds.h>

#include <cstdint>
#include <string>
#include <vector>

// Reads and changes what is installed on the console, through the AM and FS
// services, for the Titles screens. Only user software is ever listed or
// touched (see titles::isShown).
namespace library {

struct ExtData {
    uint64_t id = 0;
    // From the extra data's own icon, which stays when the game is gone.
    std::string name;
    std::string publisher;
    std::vector<uint16_t> icon;
};

struct PendingTitle {
    uint64_t id = 0;
    FS_MediaType media = MEDIATYPE_SD;
};

struct Snapshot {
    std::vector<titles::Title> titles; // SD card and system memory
    std::vector<uint64_t> tickets;     // user tickets only
    std::vector<ExtData> extData;      // on the SD card
    std::vector<PendingTitle> pending; // unfinished user installs
};

// Reads everything. Slow (an icon per title), so run it on a worker.
bool load(Snapshot &out, std::string &error);

FS_MediaType mediaOf(const titles::Title &title);
// The installed title 3dsDL runs as, or 0 for the .3dsx.
uint64_t runningTitle();

bool deleteTitle(const titles::Title &title, bool withTicket,
                 std::string &error);
bool deleteTicket(uint64_t titleId, std::string &error);
bool deleteExtData(uint64_t extDataId, std::string &error);
bool deletePending(const PendingTitle &pending, std::string &error);

} // namespace library
