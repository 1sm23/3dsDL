#include "library.hpp"

#include "installer.hpp"
#include "fs_util.hpp"
#include "smdh.hpp"
#include "title_meta.hpp"
#include "updater.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

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

void loadTitles(FS_MediaType media, Snapshot &out)
{
    u32 count = 0;
    if (R_FAILED(AM_GetTitleCount(media, &count)) || count == 0) {
        return;
    }
    std::vector<u64> ids(count);
    u32 read = 0;
    if (R_FAILED(AM_GetTitleList(&read, media, count, ids.data()))) {
        return;
    }
    ids.resize(read);
    std::vector<u64> shown;
    for (const u64 id : ids) {
        if (titles::isShown(id)) {
            shown.push_back(id);
        }
    }
    if (shown.empty()) {
        return;
    }
    std::vector<AM_TitleInfo> infos(shown.size());
    const bool haveInfo = R_SUCCEEDED(AM_GetTitleInfo(
        media, static_cast<u32>(shown.size()), shown.data(), infos.data()));

    for (size_t index = 0; index < shown.size(); ++index) {
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
}

void loadTickets(Snapshot &out)
{
    u32 count = 0;
    if (R_FAILED(AM_GetTicketCount(&count)) || count == 0) {
        return;
    }
    std::vector<u64> ids(count);
    u32 read = 0;
    if (R_FAILED(AM_GetTicketList(&read, count, 0, ids.data()))) {
        return;
    }
    ids.resize(read);
    // A title can hold several tickets; deleting its ID removes them all, so
    // each title is listed once.
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (const u64 id : ids) {
        if (titles::isShown(id)) {
            out.tickets.push_back(id);
        }
    }
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

bool load(Snapshot &out, std::string &error)
{
    out = Snapshot();
    if (!installer::available()) {
        error = installer::unavailableReason();
        return false;
    }
    loadTitles(MEDIATYPE_SD, out);
    loadTitles(MEDIATYPE_NAND, out);
    // A 3DS game card in the slot, for its save data.
    loadTitles(MEDIATYPE_GAME_CARD, out);
    loadTickets(out);
    loadExtData(out);
    loadPending(MEDIATYPE_SD, out);
    loadPending(MEDIATYPE_NAND, out);
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
    const ::Result code = AM_DeleteTicket(titleId);
    if (R_FAILED(code)) {
        error = "The ticket could not be deleted. " + installer::describe(code);
        return false;
    }
    return true;
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
    AM_DeleteTicket(titleId);
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
