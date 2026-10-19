#pragma once

#include "cia.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// What 3dsDL knows about the software installed on the console, for the
// Titles screens: installed titles, tickets, extra data and seeds. Only
// data and rules live here; app/library reads them from the console.
namespace titles {

enum class Place { SdCard, SystemMemory, GameCard };

struct Title {
    uint64_t id = 0;
    Place place = Place::SdCard;
    uint16_t version = 0;
    uint64_t size = 0;
    std::string productCode; // "CTR-P-AXXE", or "" when unknown
    // From the title's own icon data; empty for updates, DLC and DSiWare.
    std::string name;
    // The English short name as stored, which Checkpoint names backup
    // folders after (see core/saves).
    std::string englishShortName;
    std::string publisher;
    std::vector<uint16_t> icon; // 48x48 RGB565, or empty
};

// The installed software that 3dsDL shows: everything but system titles.
bool isShown(uint64_t titleId);
// Software (apps, demos, DSiWare) as opposed to updates and DLC.
bool isSoftware(uint64_t titleId);

// Titles by ID, for naming updates, DLC, tickets and extra data after the
// game they belong to.
using Index = std::map<uint64_t, const Title *>;
Index indexOf(const std::vector<Title> &all);

// A name for the list: the title's own, or for an update or DLC its game's
// name, or its Title ID when nothing better is known.
std::string displayName(const Title &title, const Index &index);
std::string displayName(uint64_t titleId, const Index &index);

// Sorted by display name, ignoring case, then by Title ID.
void sortByName(std::vector<const Title *> &list, const Index &index);

// Most software keeps its extra data under its unique ID, bits 8 to 27 of
// the Title ID.
uint32_t uniqueId(uint64_t titleId);
// The installed software whose unique ID is extDataId, or nullptr.
const Title *ownerOfExtData(uint64_t extDataId, const std::vector<Title> &all);

// Tickets whose title is not installed, leaving out system tickets.
std::vector<uint64_t> unusedTickets(const std::vector<uint64_t> &tickets,
                                    const std::vector<Title> &installed);

// --- Seeds ---------------------------------------------------------------------

using Seed = std::array<uint8_t, 16>;

struct SeedEntry {
    uint64_t titleId = 0;
    Seed seed {};
};

// Reads a seeddb.bin: a little-endian count, 12 bytes of padding, then
// 32-byte entries of Title ID, seed and 8 reserved bytes. Fails with a reason
// when the size does not match the count.
bool parseSeedDb(const std::string &data, std::vector<SeedEntry> &out,
                 std::string &error);

// The entries for titles in installed, the only ones worth adding.
std::vector<SeedEntry> seedsFor(const std::vector<SeedEntry> &entries,
                                const std::vector<Title> &installed);

} // namespace titles
