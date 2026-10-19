#include "installer.hpp"

#include "cia.hpp"
#include "fs_util.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace installer {
namespace {

// Big enough that the SD card and AM each see few, large requests.
constexpr size_t ChunkSize = 512 * 1024;
constexpr size_t SeedSize = 16;
// AM's "invalid or outdated title version".
constexpr int VersionRefused = 39;

// Nintendo's seed server presents a certificate from Nintendo's own CA,
// which is not in the Mozilla bundle. Its public key is pinned instead;
// the certificate is valid until 2038.
constexpr const char *SeedServerKey =
    "sha256//G2ME0Jc6NyYHoLAj46YE32ygTCPG6xPvZxQhtXn3Eqs=";

bool started = false;
::Result startResult = 0;

std::string hexCode(::Result code)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX",
                  static_cast<unsigned long>(code));
    return buffer;
}

// FS_Path wants UTF-16 without the "sdmc:" device name.
class SdPath {
public:
    explicit SdPath(const std::string &path)
    {
        std::string local = path;
        if (local.compare(0, 5, "sdmc:") == 0) {
            local.erase(0, 5);
        }
        units_.assign(local.size() + 1, 0);
        const ssize_t count = utf8_to_utf16(
            units_.data(), reinterpret_cast<const uint8_t *>(local.c_str()),
            local.size());
        valid_ = count >= 0;
        units_.resize(valid_ ? static_cast<size_t>(count) + 1 : 1);
        units_.back() = 0;
    }

    bool valid() const { return valid_; }
    FS_Path path() const
    {
        return {PATH_UTF16, static_cast<u32>(units_.size() * 2),
                units_.data()};
    }

private:
    std::vector<uint16_t> units_;
    bool valid_ = false;
};

class FileHandle {
public:
    FileHandle() = default;
    ~FileHandle()
    {
        if (handle_ != 0) {
            FSFILE_Close(handle_);
        }
    }
    FileHandle(const FileHandle &) = delete;
    FileHandle &operator=(const FileHandle &) = delete;

    ::Result open(const std::string &path)
    {
        const SdPath local(path);
        if (!local.valid()) {
            return MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_APPLICATION,
                              RD_INVALID_COMBINATION);
        }
        return FSUSER_OpenFileDirectly(&handle_, ARCHIVE_SDMC,
                                       fsMakePath(PATH_EMPTY, ""),
                                       local.path(), FS_OPEN_READ, 0);
    }
    Handle get() const { return handle_; }

private:
    Handle handle_ = 0;
};

bool isVersionRefusal(::Result code)
{
    return (R_MODULE(code) == RM_AM || R_MODULE(code) == RM_AM_LOW) &&
           R_DESCRIPTION(code) == VersionRefused;
}

Result failure(::Result code, const std::string &what)
{
    Result result;
    result.code = code;
    result.versionRefused = isVersionRefusal(code);
    result.message = what + " " + describe(code);
    return result;
}

// FBI's FSUSER_AddSeed, which libctru does not wrap.
::Result addSeed(uint64_t titleId, const uint8_t *seed)
{
    u32 *command = getThreadCommandBuffer();
    command[0] = IPC_MakeHeader(0x87a, 6, 0);
    command[1] = static_cast<u32>(titleId);
    command[2] = static_cast<u32>(titleId >> 32);
    std::memcpy(&command[3], seed, SeedSize);
    const ::Result sent = svcSendSyncRequest(*fsGetSessionHandle());
    return R_FAILED(sent) ? sent : static_cast<::Result>(command[1]);
}

bool readSeedFile(const std::string &path, uint8_t *seed)
{
    std::string contents;
    if (!fs::readFile(path, contents) || contents.size() != SeedSize) {
        return false;
    }
    std::memcpy(seed, contents.data(), SeedSize);
    return true;
}

// The country code Nintendo's server expects for the console's region.
const char *seedCountry()
{
    static const char *const Countries[] = {"JP", "US", "GB", "GB",
                                            "HK", "KR", "TW"};
    u8 region = 1;
    if (R_SUCCEEDED(cfguInit())) {
        if (R_FAILED(CFGU_SecureInfoGetRegion(&region))) {
            region = 1;
        }
        cfguExit();
    }
    return region < 7 ? Countries[region] : "US";
}

size_t collect(char *data, size_t size, size_t count, void *output)
{
    std::string &body = *static_cast<std::string *>(output);
    if (body.size() + size * count > 64) {
        return 0; // a seed is 16 bytes; anything this big is not one
    }
    body.append(data, size * count);
    return size * count;
}

bool downloadSeed(uint64_t titleId, uint8_t *seed, std::string &message)
{
    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
        message = "Could not start the seed download.";
        return false;
    }
    const std::string link =
        "https://kagiya-ctr.cdn.nintendo.net/title/0x" +
        cia::titleIdText(titleId) + "/ext_key?country=" + seedCountry();
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, link.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, SeedServerKey);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        message = std::string("Could not reach Nintendo's seed server: ") +
                  curl_easy_strerror(code) + ".";
        return false;
    }
    if (status == 404) {
        message = "Nintendo's server has no seed for this title.";
        return false;
    }
    if (status != 200 || body.size() != SeedSize) {
        message = "Nintendo's seed server answered with HTTP " +
                  std::to_string(status) + ".";
        return false;
    }
    std::memcpy(seed, body.data(), SeedSize);
    return true;
}

} // namespace

void init()
{
    startResult = amInit();
    started = R_SUCCEEDED(startResult);
    if (started) {
        // Creates the SD card's title database when it is missing, as on a
        // card that has never had a title installed. Leaves one that exists.
        AM_InitializeExternalTitleDatabase(false);
    }
}

void exit()
{
    if (started) {
        amExit();
        started = false;
    }
}

bool available()
{
    return started;
}

std::string unavailableReason()
{
    return "3dsDL could not open the install service (" +
           hexCode(startResult) +
           "). Run the CIA version of 3dsDL, or the .3dsx from the Homebrew "
           "Launcher with Luma3DS.";
}

FS_MediaType destination(uint64_t titleId)
{
    return cia::installsToNand(titleId) ? MEDIATYPE_NAND : MEDIATYPE_SD;
}

Result installCia(const std::string &path, uint64_t titleId,
                  Observer &observer)
{
    if (!started) {
        Result result;
        result.message = unavailableReason();
        return result;
    }

    FileHandle file;
    ::Result code = file.open(path);
    if (R_FAILED(code)) {
        return failure(code, "Could not open the CIA file.");
    }
    u64 size = 0;
    code = FSFILE_GetSize(file.get(), &size);
    if (R_FAILED(code)) {
        return failure(code, "Could not read the CIA file.");
    }

    std::unique_ptr<uint8_t, decltype(&std::free)> buffer(
        static_cast<uint8_t *>(std::malloc(ChunkSize)), &std::free);
    if (!buffer) {
        Result result;
        result.message = "Not enough memory to install.";
        return result;
    }

    const u64 startTime = osGetTime();
    Handle target = 0;
    code = AM_StartCiaInstall(destination(titleId), &target);
    if (R_FAILED(code)) {
        return failure(code, "The install could not start.");
    }

    observer.onProgress(0, size);
    u64 offset = 0;
    while (offset < size) {
        if (observer.cancelRequested()) {
            AM_CancelCIAInstall(target);
            Result result;
            result.outcome = Outcome::Cancelled;
            result.message = "The install was cancelled. Nothing was "
                             "changed.";
            return result;
        }
        const u32 wanted = static_cast<u32>(
            std::min<u64>(ChunkSize, size - offset));
        u32 read = 0;
        code = FSFILE_Read(file.get(), &read, offset, buffer.get(), wanted);
        if (R_SUCCEEDED(code) && read != wanted) {
            code = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION,
                              RD_NO_DATA);
        }
        if (R_FAILED(code)) {
            AM_CancelCIAInstall(target);
            return failure(code, "Reading the CIA file failed.");
        }
        u32 written = 0;
        code = FSFILE_Write(target, &written, offset, buffer.get(), read, 0);
        if (R_FAILED(code)) {
            AM_CancelCIAInstall(target);
            return failure(code, "The install failed.");
        }
        offset += read;
        observer.onProgress(offset, size);
    }

    code = AM_FinishCiaInstall(target);
    if (R_FAILED(code)) {
        return failure(code, "The install could not be finished.");
    }

    Result result;
    result.outcome = Outcome::Installed;
    result.bytes = size;
    result.seconds = static_cast<double>(osGetTime() - startTime) / 1000.0;
    return result;
}

bool installedVersion(uint64_t titleId, uint16_t &version)
{
    if (!started) {
        return false;
    }
    u64 id = titleId;
    AM_TitleInfo info {};
    if (R_FAILED(AM_GetTitleInfo(destination(titleId), 1, &id, &info))) {
        return false;
    }
    version = info.version;
    return true;
}

bool removeTitle(uint64_t titleId, std::string &error)
{
    if (cia::isSystemTitle(titleId)) {
        error = "System titles are never removed.";
        return false;
    }
    const ::Result code = AM_DeleteTitle(destination(titleId), titleId);
    if (R_FAILED(code)) {
        error = "Could not remove the installed title. " + describe(code);
        return false;
    }
    return true;
}

int64_t requiredSpace(const std::string &path, uint64_t titleId)
{
    if (!started) {
        return -1;
    }
    FileHandle file;
    u64 space = 0;
    if (R_FAILED(file.open(path)) ||
        R_FAILED(AM_GetCiaRequiredSpace(&space, destination(titleId),
                                        file.get()))) {
        return -1;
    }
    return static_cast<int64_t>(space);
}

bool installSeed(uint64_t titleId, std::string &message)
{
    const std::string name = cia::titleIdText(titleId) + ".dat";
    uint8_t seed[SeedSize];
    std::string source;
    if (readSeedFile("sdmc:/3ds/3dsDL/seeds/" + name, seed) ||
        readSeedFile("sdmc:/fbi/seed/" + name, seed)) {
        source = "from the SD card";
    } else if (downloadSeed(titleId, seed, message)) {
        source = "from Nintendo";
    } else {
        return false;
    }

    const ::Result code = addSeed(titleId, seed);
    if (R_FAILED(code)) {
        message = "The seed could not be added. " + describe(code);
        return false;
    }
    message = "Added the game's seed " + source + ".";
    return true;
}

bool addSeed(uint64_t titleId, const uint8_t *seed, std::string &error)
{
    const ::Result code = addSeed(titleId, seed);
    if (R_FAILED(code)) {
        error = "The seed could not be added. " + describe(code);
        return false;
    }
    return true;
}

bool launch(uint64_t titleId, std::string &error)
{
    // The jump carries no parameters; both buffers stay zero.
    static u8 parameters[0x300];
    static u8 hmac[0x20];
    ::Result code = APT_PrepareToDoApplicationJump(
        0, titleId, static_cast<u8>(destination(titleId)));
    if (R_SUCCEEDED(code)) {
        code = APT_DoApplicationJump(parameters, sizeof(parameters), hmac);
    }
    if (R_FAILED(code)) {
        error = "Could not start the title. " + describe(code);
        return false;
    }
    return true;
}

std::string describe(::Result code)
{
    const int module = R_MODULE(code);
    const int description = R_DESCRIPTION(code);
    std::string text;
    if (module == RM_AM || module == RM_AM_LOW) {
        switch (description) {
        case 4:
            text = "The install service was busy with something else. "
                   "Restart the console and try again.";
            break;
        case 37:
            text = "The CIA's contents are damaged.";
            break;
        case 39:
            text = "The install service refused this version. The same or "
                   "a newer version may already be installed.";
            break;
        case 43:
            text = "The SD card's title database is missing.";
            break;
        case 44:
            text = "System titles cannot be removed.";
            break;
        case 53:
            text = "There is not enough space left in the DSiWare area of "
                   "the system memory.";
            break;
        case 106:
            text = "The CIA failed a signature or hash check. The file is "
                   "probably damaged; download it again.";
            break;
        default:
            break;
        }
    } else if (module == RM_FS && description == RD_NOT_FOUND) {
        text = "The file was not found.";
    }
    if (text.empty()) {
        text = "Error " + hexCode(code) + ".";
    } else {
        text += " (" + hexCode(code) + ")";
    }
    return text;
}

} // namespace installer
