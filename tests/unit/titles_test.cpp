#include "../check.hpp"

#include "titles.hpp"

#include <string>
#include <vector>

namespace {

constexpr uint64_t Game = 0x0004000000055D00ULL;
constexpr uint64_t GameUpdate = 0x0004000E00055D00ULL;
constexpr uint64_t GameDlc = 0x0004008C00055D00ULL;
constexpr uint64_t Demo = 0x0004000200123400ULL;
constexpr uint64_t DsiWare = 0x00048004484E4741ULL;
constexpr uint64_t HomeMenu = 0x0004003000008F02ULL;
constexpr uint64_t SystemDsiWare = 0x0004800542383841ULL;
constexpr uint64_t DownloadPlayChild = 0x0004000100055D00ULL;

titles::Title title(uint64_t id, const std::string &name)
{
    titles::Title value;
    value.id = id;
    value.name = name;
    return value;
}

void putLe(std::string &data, size_t offset, uint64_t value, int bytes)
{
    for (int index = 0; index < bytes; ++index) {
        data[offset + index] = static_cast<char>(value >> (8 * index));
    }
}

} // namespace

TEST("titles: only user software is shown")
{
    for (const uint64_t id : {Game, GameUpdate, GameDlc, Demo, DsiWare}) {
        CHECK(titles::isShown(id));
    }
    for (const uint64_t id : {HomeMenu, SystemDsiWare, DownloadPlayChild,
                              0x0003000548414E41ULL}) {
        CHECK(!titles::isShown(id));
    }
    CHECK(titles::isSoftware(Game));
    CHECK(titles::isSoftware(Demo));
    CHECK(titles::isSoftware(DsiWare));
    CHECK(!titles::isSoftware(GameUpdate));
    CHECK(!titles::isSoftware(GameDlc));
    CHECK(!titles::isSoftware(HomeMenu));
}

TEST("titles: updates and DLC are named after their game")
{
    const std::vector<titles::Title> all = {
        title(Game, "Pilotwings"), title(GameUpdate, ""), title(GameDlc, ""),
        title(Demo, "")};
    const titles::Index index = titles::indexOf(all);
    CHECK_EQ(titles::displayName(all[1], index), std::string("Pilotwings"));
    CHECK_EQ(titles::displayName(all[2], index), std::string("Pilotwings"));
    CHECK_EQ(titles::displayName(all[3], index),
             std::string("0004000200123400"));
    // A ticket for an update whose game is gone.
    CHECK_EQ(titles::displayName(0x0004000E00099900ULL, index),
             std::string("0004000E00099900"));
}

TEST("titles: sorts by name without case, then by ID")
{
    const std::vector<titles::Title> all = {
        title(Demo, "beta"), title(Game, "Alpha"), title(DsiWare, "alpha"),
        title(GameUpdate, "")};
    const titles::Index index = titles::indexOf(all);
    std::vector<const titles::Title *> list;
    for (const titles::Title &value : all) {
        list.push_back(&value);
    }
    titles::sortByName(list, index);
    CHECK_EQ(list[0]->id, Game);
    CHECK_EQ(list[1]->id, GameUpdate); // named "Alpha" after its game
    CHECK_EQ(list[2]->id, DsiWare);
    CHECK_EQ(list[3]->id, Demo);
}

TEST("titles: extra data belongs to the software with its unique ID")
{
    const std::vector<titles::Title> all = {title(GameUpdate, ""),
                                            title(Game, "Pilotwings")};
    CHECK_EQ(titles::uniqueId(Game), static_cast<uint32_t>(0x55D));
    const titles::Title *owner = titles::ownerOfExtData(0x55D, all);
    CHECK(owner != nullptr && owner->id == Game);
    CHECK(titles::ownerOfExtData(0x777, all) == nullptr);
}

TEST("titles: unused tickets leave out installed and system titles")
{
    const std::vector<titles::Title> installed = {title(Game, "Pilotwings")};
    const std::vector<uint64_t> unused = titles::unusedTickets(
        {Game, GameUpdate, HomeMenu, Demo}, installed);
    CHECK_EQ(unused.size(), static_cast<size_t>(2));
    CHECK_EQ(unused[0], GameUpdate);
    CHECK_EQ(unused[1], Demo);
}

TEST("titles: reads seeddb.bin and keeps seeds for installed software")
{
    std::string data(16 + 3 * 32, '\0');
    putLe(data, 0, 3, 4);
    const uint64_t ids[] = {Demo, Game, Game};
    for (int entry = 0; entry < 3; ++entry) {
        putLe(data, 16 + entry * 32, ids[entry], 8);
        for (int byte = 0; byte < 16; ++byte) {
            data[16 + entry * 32 + 8 + byte] = static_cast<char>(entry * 16 + byte);
        }
    }
    std::vector<titles::SeedEntry> entries;
    std::string error;
    CHECK(titles::parseSeedDb(data, entries, error));
    CHECK_EQ(entries.size(), static_cast<size_t>(3));
    CHECK_EQ(entries[1].titleId, Game);
    CHECK_EQ(static_cast<int>(entries[1].seed[0]), 16);
    CHECK_EQ(static_cast<int>(entries[1].seed[15]), 31);

    const std::vector<titles::SeedEntry> wanted =
        titles::seedsFor(entries, {title(Game, "Pilotwings"), title(GameDlc, "")});
    CHECK_EQ(wanted.size(), static_cast<size_t>(1));
    CHECK_EQ(wanted[0].titleId, Game);
    CHECK_EQ(static_cast<int>(wanted[0].seed[0]), 16);

    CHECK(!titles::parseSeedDb(data.substr(0, data.size() - 1), entries, error));
    CHECK(error.find("3 seeds") != std::string::npos);
    CHECK(!titles::parseSeedDb("short", entries, error));
    std::string empty(16, '\0');
    CHECK(titles::parseSeedDb(empty, entries, error));
    CHECK(entries.empty());
}
