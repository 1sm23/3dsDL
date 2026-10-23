#include "library.hpp"

#include "cia.hpp"
#include "installer.hpp"
#include "fs_util.hpp"
#include "smdh.hpp"
#include "text.hpp"
#include "title_meta.hpp"
#include "updater.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <set>

namespace library {

namespace {

constexpr u32 SmdhSize = 0x36C0;
constexpr u32 MaxExtData = 1024;

bool readSmdh(Handle file, smdh::Info &info)
{
    std::unique_ptr<uint8_t[]> data(new uint8_t[SmdhSize]);
    u32 read = 0;
    const bool ok = R_SUCCEEDED(FSFILE_Read(file, &read, 0, data.get(), SmdhSize)) &&
                    read == SmdhSize && smdh::parse(data.get(), SmdhSize, info);
    FSFILE_Close(file);
    return ok;
}

// The icon in a title's ExeFS, the way FBI reads it.
bool titleSmdh(uint64_t titleId, FS_MediaType media, smdh::Info &info)
{
    static const u32 filePath[5] = {0x00000000, 0x00000000, 0x00000002,
                                    0x6E6F6369 /* "icon" */, 0x00000000};
    const u32 archivePath[4] = {static_cast<u32>(titleId),
                                static_cast<u32>(titleId >> 32),
                                static_cast<u32>(media), 0};
    Handle file = 0;
    if (R_FAILED(FSUSER_OpenFileDirectly(
            &file, ARCHIVE_SAVEDATA_AND_CONTENT,
            {PATH_BINARY, sizeof(archivePath), archivePath},
            {PATH_BINARY, sizeof(filePath), filePath}, FS_OPEN_READ, 0))) {
        return false;
    }
    return readSmdh(file, info);
}

FS_ExtSaveDataInfo extDataInfo(uint64_t id)
{
    FS_ExtSaveDataInfo info {};
    info.mediaType = MEDIATYPE_SD;
    info.saveId = id;
    return info;
}

// Counts the titles each list holds, for a progress bar over all of them.
struct Counter {
    const std::atomic<bool> *stop = nullptr;
    std::atomic<uint64_t> *done = nullptr;

    bool stopped() const { return stop != nullptr && stop->load(); }
    void add(uint64_t count) const
    {
        if (done != nullptr) {
            done->fetch_add(count);
        }
    }
};

#ifdef THREEDSDL_FAULTS
// Test builds only: each line of sdmc:/3ds/3dsDL/faults.txt makes one of
// the reads below go wrong the way a console can, so the safety checks
// around deleting can be tried without breaking a real console.
bool fault(const char *name)
{
    std::string text;
    if (!fs::readFile("sdmc:/3ds/3dsDL/faults.txt", text)) {
        return false;
    }
    const std::string wanted = std::string(name);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line == wanted) {
            return true;
        }
        start = end + 1;
    }
    return false;
}
#else
constexpr bool fault(const char *)
{
    return false;
}
#endif

// What the deleting code saw and did, kept with the backups for reports.
void logTickets(const std::string &line)
{
    if (!fs::makeDirectories(DeletedTicketsFolder)) {
        return;
    }
    const std::string path = std::string(DeletedTicketsFolder) + "log.txt";
    FILE *file = std::fopen(path.c_str(), "a");
    if (file == nullptr) {
        return;
    }
    char stamp[32];
    const time_t now = std::time(nullptr);
    tm parts {};
    gmtime_r(&now, &parts);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &parts);
    std::fprintf(file, "%s %s %s\n", stamp, APP_VERSION, line.c_str());
    std::fclose(file);
}

// Whether the SD card's title database is there for AM to answer from.
bool sdDatabaseAvailable()
{
    bool available = false;
    if (R_FAILED(AM_QueryAvailableExternalTitleDatabase(&available)) ||
        fault("sd-db-unavailable")) {
        return false;
    }
    return available;
}

u32 titleCount(FS_MediaType media)
{
    u32 count = 0;
    return R_SUCCEEDED(AM_GetTitleCount(media, &count)) ? count : 0;
}

// Every title ID on media. Fails rather than returning fewer when AM
// cannot say: a short list makes the tickets of installed software look
// unused, and deleting those leaves the software unable to start.
bool titleIds(FS_MediaType media, std::vector<u64> &ids, std::string &error)
{
    ids.clear();
    // Without the SD card's title database AM still answers, with no
    // titles at all, rather than failing (faketik learnt this too).
    if (media == MEDIATYPE_SD && !sdDatabaseAvailable()) {
        error = "The console cannot read the list of software on the SD "
                "card right now, so 3dsDL cannot tell what is installed.";
        return false;
    }
    if (media == MEDIATYPE_SD && fault("sd-list-empty")) {
        return true; // the answer that deleted a user's tickets
    }
    u32 count = 0;
    ::Result code = fault(media == MEDIATYPE_SD ? "sd-list-error"
                                                : "nand-list-error")
                        ? -1
                        : AM_GetTitleCount(media, &count);
    if (R_SUCCEEDED(code) && count > 0) {
        ids.resize(count);
        u32 read = 0;
        code = AM_GetTitleList(&read, media, count, ids.data());
        if (R_SUCCEEDED(code) && read != count) {
            code = -1; // it changed while being read
        }
    }
    if (R_FAILED(code)) {
        ids.clear();
        error = std::string("Could not read the software installed ") +
                (media == MEDIATYPE_NAND ? "in system memory" : "on the SD card") +
                ". " + installer::describe(code);
        return false;
    }
    return true;
}

// Every ticket's title ID, each once. Fails rather than returning fewer.
bool ticketIds(std::vector<u64> &ids, std::string &error)
{
    ids.clear();
    u32 count = 0;
    ::Result code =
        fault("ticket-list-error") ? -1 : AM_GetTicketCount(&count);
    if (R_SUCCEEDED(code) && count > 0) {
        ids.resize(count);
        u32 read = 0;
        code = AM_GetTicketList(&read, count, 0, ids.data());
        if (R_SUCCEEDED(code) && read != count) {
            code = -1;
        }
    }
    if (R_FAILED(code)) {
        ids.clear();
        error = "Could not read the tickets. " + installer::describe(code);
        return false;
    }
    // A title can hold several tickets; deleting its ID removes them all, so
    // each title is listed once.
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return true;
}

// Titles installed on the SD card by their own files: a folder Nintendo
// 3DS/<id0>/<id1>/title/<high>/<low>/content holding a .tmd, for every
// console the card has been in. Read from the card itself, so it does not
// depend on AM listing them. Folders left behind without one, as after a
// delete, do not count.
bool titlesOnSdCard(std::set<uint64_t> &out, std::string &error)
{
    out.clear();
    if (fault("folder-scan-error")) {
        error = "Could not read sdmc:/Nintendo 3DS/ (test fault).";
        return false;
    }
    const std::string root = "sdmc:/Nintendo 3DS/";
    if (!fs::directoryExists(root)) {
        return true; // a card that has never held software
    }
    const auto folders = [&](const std::string &path,
                             std::vector<std::string> &names) {
        bool ok = false;
        names.clear();
        for (const fs::Entry &entry : fs::listEntries(path, &ok, true, false)) {
            if (entry.folder) {
                names.push_back(entry.name);
            }
        }
        if (!ok) {
            error = "Could not read " + path + ".";
        }
        return ok;
    };
    std::vector<std::string> id0s;
    if (!folders(root, id0s)) {
        return false;
    }
    std::vector<std::string> id1s;
    std::vector<std::string> highs;
    std::vector<std::string> lows;
    for (const std::string &id0 : id0s) {
        if (!folders(root + id0 + "/", id1s)) {
            return false;
        }
        for (const std::string &id1 : id1s) {
            const std::string titleRoot = root + id0 + "/" + id1 + "/title/";
            if (!fs::directoryExists(titleRoot)) {
                continue;
            }
            if (!folders(titleRoot, highs)) {
                return false;
            }
            for (const std::string &high : highs) {
                if (!folders(titleRoot + high + "/", lows)) {
                    return false;
                }
                for (const std::string &low : lows) {
                    uint64_t id = 0;
                    if (!titles::titleFromSdFolders(high, low, id)) {
                        continue;
                    }
                    const std::string content =
                        titleRoot + high + "/" + low + "/content/";
                    if (!fs::directoryExists(content)) {
                        continue;
                    }
                    bool ok = false;
                    const std::vector<fs::Entry> files =
                        fs::listEntries(content, &ok, true, false);
                    if (!ok) {
                        error = "Could not read " + content + ".";
                        return false;
                    }
                    for (const fs::Entry &file : files) {
                        if (!file.folder && file.name.size() > 4 &&
                            text::endsWithNoCase(file.name, ".tmd")) {
                            out.insert(id);
                            break;
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool loadTitles(FS_MediaType media, Snapshot &out, const Counter &counter,
                std::string &error)
{
    std::vector<u64> ids;
    if (!titleIds(media, ids, error)) {
        return false;
    }
    std::vector<u64> shown;
    for (const u64 id : ids) {
        if (titles::isShown(id)) {
            shown.push_back(id);
        }
    }
    counter.add(ids.size() - shown.size());
    if (shown.empty()) {
        return true;
    }
    std::vector<AM_TitleInfo> infos(shown.size());
    const bool haveInfo = R_SUCCEEDED(AM_GetTitleInfo(
        media, static_cast<u32>(shown.size()), shown.data(), infos.data()));

    for (size_t index = 0; index < shown.size(); ++index) {
        if (counter.stopped()) {
            return true; // load sees the stop and keeps nothing
        }
        counter.add(1);
        titles::Title title;
        title.id = shown[index];
        title.place = media == MEDIATYPE_NAND ? titles::Place::SystemMemory
                      : media == MEDIATYPE_GAME_CARD ? titles::Place::GameCard
                                                     : titles::Place::SdCard;
        if (haveInfo) {
            title.version = infos[index].version;
            title.size = infos[index].size;
        }
        if (titles::isSoftware(title.id)) {
            char code[32] = {};
            if (R_SUCCEEDED(AM_GetTitleProductCode(media, title.id, code))) {
                title.productCode = code;
            }
            smdh::Info info;
            if (titleSmdh(title.id, media, info)) {
                title.name = info.shortName.empty() ? info.longName
                                                    : info.shortName;
                title.publisher = info.publisher;
                title.englishShortName = info.englishShortName;
                title.icon = std::move(info.icon);
            }
        }
        out.titles.push_back(std::move(title));
    }
    return true;
}

bool loadTickets(Snapshot &out, std::string &error)
{
    std::vector<u64> ids;
    if (!ticketIds(ids, error)) {
        return false;
    }
    for (const u64 id : ids) {
        if (titles::isShown(id)) {
            out.tickets.push_back(id);
        }
    }
    return true;
}

void loadExtData(Snapshot &out)
{
    std::vector<u64> ids(MaxExtData);
    u32 count = 0;
    if (R_FAILED(FSUSER_EnumerateExtSaveData(
            &count, MaxExtData * sizeof(u64), MEDIATYPE_SD, sizeof(u64), false,
            reinterpret_cast<u8 *>(ids.data())))) {
        return;
    }
    std::unique_ptr<uint8_t[]> data(new uint8_t[SmdhSize]);
    for (u32 index = 0; index < count; ++index) {
        if (ids[index] == 0) {
            continue; // not a real extra data ID
        }
        ExtData extData;
        extData.id = ids[index];
        u32 read = 0;
        smdh::Info info;
        if (R_SUCCEEDED(FSUSER_ReadExtSaveDataIcon(&read, extDataInfo(extData.id),
                                                   SmdhSize, data.get())) &&
            smdh::parse(data.get(), read, info)) {
            extData.name = info.shortName.empty() ? info.longName : info.shortName;
            extData.englishShortName = info.englishShortName;
            extData.publisher = info.publisher;
            extData.icon = std::move(info.icon);
        }
        out.extData.push_back(std::move(extData));
    }
}

void loadPending(FS_MediaType media, Snapshot &out)
{
    u32 count = 0;
    if (R_FAILED(AM_GetNumPendingTitles(&count, media)) || count == 0) {
        return;
    }
    std::vector<u64> ids(count);
    u32 read = 0;
    if (R_FAILED(AM_GetPendingTitleList(&read, ids.data(), count, media))) {
        return;
    }
    for (u32 index = 0; index < read; ++index) {
        if (titles::isShown(ids[index])) {
            out.pending.push_back({ids[index], media});
        }
    }
}

} // namespace

bool load(Snapshot &out, std::string &error, const std::atomic<bool> *stop,
          std::atomic<uint64_t> *done, std::atomic<uint64_t> *total)
{
    if (!installer::available()) {
        error = installer::unavailableReason();
        return false;
    }
    const Counter counter {stop, done};
    if (total != nullptr) {
        total->store(titleCount(MEDIATYPE_SD) + titleCount(MEDIATYPE_NAND) +
                     titleCount(MEDIATYPE_GAME_CARD));
    }
    // Anything that cannot be read fails the whole load: screens decide
    // what is unused from this, and a partial list would mislead them.
    Snapshot read;
    if (sdDatabaseAvailable()) {
        if (!loadTitles(MEDIATYPE_SD, read, counter, error)) {
            return false;
        }
        read.sdListed = true;
    }
    if (!loadTitles(MEDIATYPE_NAND, read, counter, error)) {
        return false;
    }
    // A 3DS game card in the slot, for its save data. With no card in the
    // slot there is nothing to read, which is not a failure.
    std::string noCard;
    loadTitles(MEDIATYPE_GAME_CARD, read, counter, noCard);
    if (counter.stopped()) {
        error.clear();
        return false;
    }
    if (!loadTickets(read, error) || !titlesOnSdCard(read.onSdCard, error)) {
        return false;
    }
    loadExtData(read);
    loadPending(MEDIATYPE_SD, read);
    loadPending(MEDIATYPE_NAND, read);
    out = std::move(read);
    return true;
}

FS_MediaType mediaOf(const titles::Title &title)
{
    switch (title.place) {
    case titles::Place::SystemMemory:
        return MEDIATYPE_NAND;
    case titles::Place::GameCard:
        return MEDIATYPE_GAME_CARD;
    case titles::Place::SdCard:
        break;
    }
    return MEDIATYPE_SD;
}

uint64_t runningTitle()
{
    if (!updater::runningAsCia()) {
        return 0;
    }
    u64 id = 0;
    return R_SUCCEEDED(APT_GetProgramID(&id)) ? id : 0;
}

bool deleteTitle(const titles::Title &title, bool withTicket, std::string &error)
{
    if (!titles::isShown(title.id)) {
        error = "System titles are never removed.";
        return false;
    }
    if (title.id == runningTitle()) {
        error = "3dsDL cannot delete itself while it is running.";
        return false;
    }
    if (title.place == titles::Place::GameCard) {
        error = "Software on a game card cannot be deleted.";
        return false;
    }
    const ::Result code = AM_DeleteTitle(mediaOf(title), title.id);
    if (R_FAILED(code)) {
        error = "It could not be deleted. " + installer::describe(code);
        return false;
    }
    if (withTicket) {
        // A title can be installed without a ticket of its own, so a
        // missing ticket is not a failure.
        AM_DeleteTicket(title.id);
    }
    return true;
}

std::vector<uint64_t> unusedTickets(const Snapshot &snapshot)
{
    if (!snapshot.sdListed) {
        return {};
    }
    return titles::unusedTickets(snapshot.tickets, snapshot.titles,
                                 snapshot.onSdCard);
}

bool extDataInUse(const Snapshot &snapshot, uint64_t extDataId)
{
    return !snapshot.sdListed ||
           titles::extDataInUse(extDataId, snapshot.titles, snapshot.onSdCard);
}

bool installedAnywhere(uint64_t titleId)
{
    if (fault("title-info-installed")) {
        return true;
    }
    for (const FS_MediaType media : {MEDIATYPE_SD, MEDIATYPE_NAND}) {
        u64 id = titleId;
        AM_TitleInfo info {};
        if (R_SUCCEEDED(AM_GetTitleInfo(media, 1, &id, &info))) {
            return true;
        }
    }
    return false;
}

bool checkUnusedTickets(std::vector<uint64_t> &unused, std::string &error)
{
    unused.clear();
    if (!installer::available()) {
        error = installer::unavailableReason();
        return false;
    }
    std::vector<u64> tickets;
    std::vector<u64> sd;
    std::vector<u64> nand;
    std::set<uint64_t> onSdCard;
    if (!ticketIds(tickets, error) || !titleIds(MEDIATYPE_SD, sd, error) ||
        !titleIds(MEDIATYPE_NAND, nand, error) ||
        !titlesOnSdCard(onSdCard, error)) {
        return false;
    }
    logTickets("check: tickets " + std::to_string(tickets.size()) +
               ", sd titles " + std::to_string(sd.size()) + ", nand titles " +
               std::to_string(nand.size()) + ", on card " +
               std::to_string(onSdCard.size()));
    std::vector<titles::Title> installed;
    for (const std::vector<u64> *list : {&sd, &nand}) {
        for (const u64 id : *list) {
            titles::Title title;
            title.id = id;
            installed.push_back(title);
        }
    }
    for (const uint64_t id : titles::unusedTickets(tickets, installed, onSdCard)) {
        // Asked a second way: if AM knows the title after all, the lists
        // above cannot be trusted, and nothing is deleted.
        if (installedAnywhere(id)) {
            unused.clear();
            error = cia::titleIdText(id) + " is installed but was not listed, "
                    "so the installed software could not be read reliably.";
            logTickets("check: stopped, " + cia::titleIdText(id) +
                       " is installed but was not listed");
            return false;
        }
        unused.push_back(id);
    }
    logTickets("check: " + std::to_string(unused.size()) + " unused");
    return true;
}

namespace {

bool hasTicket(uint64_t titleId)
{
    u64 ticketIds[8] = {};
    u32 count = 0;
    return R_SUCCEEDED(
               AMNET_GetTitleTicketIds(&count, ticketIds, 8, titleId, false)) &&
           count > 0;
}

bool installTicketData(const std::string &data, std::string &error)
{
    Handle ticket = 0;
    ::Result code = AMNET_InstallTicketBegin(&ticket);
    if (R_FAILED(code)) {
        error = "The ticket install could not start. " + installer::describe(code);
        return false;
    }
    u32 written = 0;
    code = FSFILE_Write(ticket, &written, 0, data.data(),
                        static_cast<u32>(data.size()), 0);
    if (R_FAILED(code)) {
        AMNET_InstallTicketAbort(ticket);
        error = "The ticket could not be installed. " + installer::describe(code);
        return false;
    }
    code = AMNET_InstallTicketFinish(ticket);
    if (R_FAILED(code)) {
        error = "The ticket could not be installed. " + installer::describe(code);
        return false;
    }
    return true;
}

} // namespace

bool deleteTicket(uint64_t titleId, std::string &error)
{
    if (!titles::isShown(titleId)) {
        error = "System tickets are never removed.";
        return false;
    }
    if (titleId == runningTitle()) {
        error = "3dsDL needs its own ticket to start.";
        return false;
    }
    // Saved first, so a deleted ticket can always be put back. No copy, no
    // delete.
    std::string backup = "nothing (test fault)";
    if (!fault("skip-backup") &&
        !exportTicket(titleId, DeletedTicketsFolder, backup, error)) {
        error = "The ticket was kept, as it could not be saved first. " + error;
        logTickets("kept " + cia::titleIdText(titleId) + ": no backup");
        return false;
    }
    const ::Result code = AM_DeleteTicket(titleId);
    if (R_FAILED(code)) {
        error = "The ticket could not be deleted. " + installer::describe(code);
        return false;
    }
    logTickets("deleted " + cia::titleIdText(titleId) + ", saved as " + backup);
    return true;
}

size_t deletedTicketCount()
{
    size_t count = 0;
    for (const fs::Entry &entry :
         fs::listEntries(DeletedTicketsFolder, nullptr, true, false)) {
        if (!entry.folder && text::endsWithNoCase(entry.name, ".tik")) {
            ++count;
        }
    }
    return count;
}

size_t restoreDeletedTickets(std::string &error)
{
    size_t restored = 0;
    for (const fs::Entry &entry :
         fs::listEntries(DeletedTicketsFolder, nullptr, true, false)) {
        if (entry.folder || !text::endsWithNoCase(entry.name, ".tik")) {
            continue;
        }
        const std::string path = std::string(DeletedTicketsFolder) + entry.name;
        uint64_t titleId = 0;
        if (installTicket(path, titleId, error)) {
            // Restored: the copy is no longer needed.
            fs::removeFile(path);
            logTickets("restored " + cia::titleIdText(titleId));
            ++restored;
        }
    }
    return restored;
}

bool deleteExtData(uint64_t extDataId, std::string &error)
{
    const ::Result code = FSUSER_DeleteExtSaveData(extDataInfo(extDataId));
    if (R_FAILED(code)) {
        error = "The extra data could not be deleted. " +
                installer::describe(code);
        return false;
    }
    return true;
}

bool deletePending(const PendingTitle &pending, std::string &error)
{
    if (!titles::isShown(pending.id)) {
        error = "System installs are never removed.";
        return false;
    }
    const ::Result code = AM_DeletePendingTitle(pending.media, pending.id);
    if (R_FAILED(code)) {
        error = "The unfinished install could not be removed. " +
                installer::describe(code);
        return false;
    }
    return true;
}

bool installTicket(const std::string &path, uint64_t &titleId,
                   std::string &error)
{
    titlemeta::Meta meta;
    if (!titlemeta::read(path, filetype::Kind::Ticket, meta, error) ||
        !meta.hasTitleId) {
        if (error.empty()) {
            error = "This is not a ticket file.";
        }
        return false;
    }
    titleId = meta.titleId;
    if (!titles::isShown(titleId)) {
        error = "This ticket is for a system title, which 3dsDL does not "
                "install.";
        return false;
    }
    std::string data;
    if (!fs::readFile(path, data) || data.empty() || data.size() > 1024 * 1024) {
        error = "The ticket file could not be read.";
        return false;
    }
    // FBI replaces a ticket that is already installed by deleting it first.
    // The one there is saved before that and put back if the new one fails,
    // so a bad file never costs the console its ticket.
    std::string saved;
    std::string why;
    const bool backedUp = exportTicket(titleId, ReplacedTicketsFolder, saved, why);
    if (!backedUp && hasTicket(titleId)) {
        error = "The ticket there now could not be saved first, so nothing "
                "was changed. " + why;
        return false;
    }
    AM_DeleteTicket(titleId);
    if (installTicketData(data, error)) {
        return true;
    }
    std::string previous;
    if (backedUp && fs::readFile(saved, previous)) {
        std::string ignored;
        installTicketData(previous, ignored);
    }
    return false;
}

bool exportTicket(uint64_t titleId, const std::string &folder,
                  std::string &path, std::string &error)
{
    u64 ticketIds[8] = {};
    u32 count = 0;
    ::Result code = AMNET_GetTitleTicketIds(&count, ticketIds, 8, titleId, false);
    if (R_FAILED(code) || count == 0) {
        error = "The console did not give out this ticket. " +
                (R_FAILED(code) ? installer::describe(code) : std::string());
        return false;
    }
    std::vector<uint8_t> buffer(64 * 1024);
    u32 size = 0;
    code = AMNET_ExportLicenseTicket(&size, buffer.data(),
                                     static_cast<u32>(buffer.size()), titleId,
                                     ticketIds[0]);
    if (R_FAILED(code) || size == 0 || size > buffer.size()) {
        error = "The console did not give out this ticket. " +
                (R_FAILED(code) ? installer::describe(code) : std::string());
        return false;
    }
    // Only keep it when it reads back as this title's ticket.
    const cia::Reader reader = [&](uint64_t offset, void *out, size_t length) {
        if (offset + length > size) {
            return false;
        }
        std::memcpy(out, buffer.data() + offset, length);
        return true;
    };
    titlemeta::Meta meta;
    std::string why;
    if (!titlemeta::read(reader, size, filetype::Kind::Ticket, meta, why) ||
        !meta.hasTitleId || meta.titleId != titleId) {
        error = "The console gave back something that is not this title's "
                "ticket.";
        return false;
    }
    if (!fs::makeDirectories(folder)) {
        error = "Could not create " + folder + ".";
        return false;
    }
    path = folder + cia::titleIdText(titleId) + ".tik";
    if (!fs::writeFile(path, std::string(reinterpret_cast<const char *>(buffer.data()),
                                         size))) {
        error = "Could not write " + path + ".";
        return false;
    }
    return true;
}

} // namespace library
