#pragma once

#include "titles.hpp"

#include <3ds.h>

#include <atomic>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Reads and changes what is installed on the console, through the AM and FS
// services, for the Titles screens. Only user software is ever listed or
// touched (see titles::isShown).
namespace library {

// Every ticket 3dsDL deletes is saved here first, with a log of what was
// checked and done, so deleting can be undone.
constexpr const char *DeletedTicketsFolder = "sdmc:/3ds/3dsDL/tickets/deleted/";
// A ticket replaced by installing a .tik is saved here first.
constexpr const char *ReplacedTicketsFolder = "sdmc:/3ds/3dsDL/tickets/replaced/";

struct ExtData {
    uint64_t id = 0;
    // From the extra data's own icon, which stays when the game is gone.
    std::string name;
    std::string englishShortName; // for Checkpoint's folder name
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
    // Titles with a folder on the SD card, read from the card itself; they
    // count as installed for deciding what is unused.
    std::set<uint64_t> onSdCard;
    // False when the console's list of SD card software was not available,
    // as on a card that has never held any. Nothing is then called unused.
    bool sdListed = false;
};

// Tickets no installed software needs, by this snapshot, for showing; none
// when the SD card's software was not listed. Deleting goes through
// checkUnusedTickets, which reads everything again.
std::vector<uint64_t> unusedTickets(const Snapshot &snapshot);
// Whether extra data belongs to installed software; always true when the SD
// card's software was not listed.
bool extDataInUse(const Snapshot &snapshot, uint64_t extDataId);

// Reads everything. Slow (an icon per title), so run it on a worker. done
// and total, if given, count the titles read. Setting stop ends it early,
// returning false with an empty error and out unchanged.
bool load(Snapshot &out, std::string &error,
          const std::atomic<bool> *stop = nullptr,
          std::atomic<uint64_t> *done = nullptr,
          std::atomic<uint64_t> *total = nullptr);

FS_MediaType mediaOf(const titles::Title &title);
// The installed title 3dsDL runs as, or 0 for the .3dsx.
uint64_t runningTitle();

bool deleteTitle(const titles::Title &title, bool withTicket,
                 std::string &error);
// True when AM describes titleId as installed on the SD card or in system
// memory, asked for that one title.
bool installedAnywhere(uint64_t titleId);
// Reads everything again and returns the tickets no installed software
// needs, for deleting them. Fails, and returns none, when any part cannot
// be read or the sources disagree, rather than guessing.
bool checkUnusedTickets(std::vector<uint64_t> &unused, std::string &error);
// Saves the ticket to DeletedTicketsFolder, then deletes it. Nothing is
// deleted when it cannot be saved.
bool deleteTicket(uint64_t titleId, std::string &error);
// How many deleted tickets are saved, and putting them all back; restored
// copies are removed. Returns how many were restored.
size_t deletedTicketCount();
size_t restoreDeletedTickets(std::string &error);
bool deleteExtData(uint64_t extDataId, std::string &error);
bool deletePending(const PendingTitle &pending, std::string &error);

// Installs the ticket file at path, replacing one already there, as FBI
// does. titleId is the ticket's title.
bool installTicket(const std::string &path, uint64_t &titleId,
                   std::string &error);
// Writes the console's ticket for titleId into folder as <Title ID>.tik and
// returns its path.
bool exportTicket(uint64_t titleId, const std::string &folder,
                  std::string &path, std::string &error);

} // namespace library
