#include "library.hpp"

#include "installer.hpp"
#include "smdh.hpp"
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
    loadTickets(out);
    loadExtData(out);
    loadPending(MEDIATYPE_SD, out);
    loadPending(MEDIATYPE_NAND, out);
    return true;
}

FS_MediaType mediaOf(const titles::Title &title)
{
    return title.place == titles::Place::SystemMemory ? MEDIATYPE_NAND
                                                      : MEDIATYPE_SD;
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

} // namespace library
