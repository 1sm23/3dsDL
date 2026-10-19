#include "app.hpp"

#include "app_util.hpp"
#include "cia.hpp"
#include "finalize.hpp"
#include "fs_util.hpp"
#include "installer.hpp"
#include "naming.hpp"
#include "ntp.hpp"
#include "scanner.hpp"
#include "storage.hpp"
#include "tasks.hpp"
#include "text.hpp"
#include "timezone.hpp"
#include "ui.hpp"
#include "units.hpp"
#include "url.hpp"
#include "zip.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <map>

namespace {

using screens::addCiaParagraphs;
using screens::basename;
using screens::ciaName;
using screens::drawLabelledPanel;
using screens::plural;

constexpr const char *CaBundle = "romfs:/cacert.pem";
constexpr const char *UserAgent = "3dsDL/" APP_VERSION " (Nintendo 3DS)";
// The soc service's default socket buffers keep the TCP receive window small,
// which limits speed on links with any latency. 48 KiB measured fastest on a
// New 3DS; 64 KiB and above break connections mid-handshake.
constexpr int SocketBufferSize = 48 * 1024;
// Even so, one connection carries at most one TCP window per round trip,
// about 300 KiB/s from a server 150 ms away. Large files are split across
// several connections to add those up, staying under the six per server
// that browsers open.
constexpr int DownloadConnections = 4;
constexpr u64 StatusRefreshMs = 2000;
constexpr size_t MaxRecentFolders = 4;

struct Preset {
    const char *label;
    const char *path;
};

constexpr Preset Presets[] = {
    {"SD card root", "sdmc:/"},
    {"Downloads", "sdmc:/Downloads/"},
    {"CIA installers", "sdmc:/cias/"},
    {"Homebrew apps", "sdmc:/3ds/"},
    {"Luma3DS payloads", "sdmc:/luma/payloads/"},
    {"GodMode9 scripts", "sdmc:/gm9/scripts/"},
};

// Home screen buttons, in navigation order. The first two fill a row each;
// the rest come in pairs.
const ui::Rect HomeButtons[] = {
    {18, 34, 284, 44},   // scan
    {18, 84, 284, 44},   // enter URL
    {18, 134, 138, 40},  // history
    {164, 134, 138, 40}, // files
    {18, 182, 138, 40},  // folder
    {164, 182, 138, 40}  // settings
};
const char *const HomeLabels[] = {"SCAN QR CODE", "ENTER URL", "HISTORY",
                                  "FILES",        "FOLDER",    "SETTINGS"};
constexpr int HomeButtonCount = 6;
constexpr int HomeRows = 4;

int homeRow(int button)
{
    return button < 2 ? button : 2 + (button - 2) / 2;
}

int homeButton(int row, int column)
{
    return row < 2 ? row : 2 + (row - 2) * 2 + column;
}

std::string onOff(bool value)
{
    return value ? "On" : "Off";
}

const char *ciaActionLabel(CiaAction action)
{
    switch (action) {
    case CiaAction::Install:
        return "Install";
    case CiaAction::InstallAndDelete:
        return "Install, delete";
    case CiaAction::Keep:
        return "Keep file";
    case CiaAction::Ask:
        break;
    }
    return "Ask";
}

std::string entryName(const HistoryEntry &entry)
{
    if (!entry.path.empty()) {
        return basename(entry.path);
    }
    const std::string fromUrl = naming::chooseFilename("", entry.url);
    return fromUrl == naming::DefaultFilename ? url::host(entry.url)
                                              : fromUrl;
}

u32 statusColor(HistoryStatus status)
{
    switch (status) {
    case HistoryStatus::Saved:
        return ui::color::Text;
    case HistoryStatus::Installed:
        return ui::color::Success;
    case HistoryStatus::Failed:
        return ui::color::Danger;
    case HistoryStatus::Cancelled:
        break;
    }
    return ui::color::Muted;
}

const char *statusLabel(HistoryStatus status)
{
    switch (status) {
    case HistoryStatus::Saved:
        return "Saved";
    case HistoryStatus::Installed:
        return "Installed";
    case HistoryStatus::Failed:
        return "Failed";
    case HistoryStatus::Cancelled:
        break;
    }
    return "Cancelled";
}

HistoryEntry newEntry(const std::string &address, const std::string &folder,
                      HistoryStatus status)
{
    HistoryEntry entry;
    entry.url = address;
    entry.folder = folder;
    entry.status = status;
    return entry;
}

} // namespace

App::App()
    : settings_(storage::loadSettings()), history_(storage::loadHistory())
{
}

void App::run()
{
    while (ui::keepRunning()) {
        switch (home()) {
        case Action::Scan: {
            const std::string link = scanner::scan();
            if (!link.empty()) {
                startDownload(link, settings_.destination);
            }
            break;
        }
        case Action::EnterUrl: {
            std::string link;
            if (platform::keyboard(platform::KeyboardKind::Url,
                                   "Direct http:// or https:// file link",
                                   "https://", link)) {
                startDownload(link, settings_.destination);
            }
            break;
        }
        case Action::History:
            historyScreen();
            break;
        case Action::Files:
            filesScreen();
            break;
        case Action::Folder:
            changeDefaultFolder();
            break;
        case Action::Settings:
            settingsScreen();
            break;
        case Action::Exit:
            return;
        case Action::None:
            break;
        }
    }
}

// --- Home ------------------------------------------------------------------

void App::refreshStatus(bool force)
{
    const u64 now = osGetTime();
    if (!force && now - lastRefresh_ < StatusRefreshMs) {
        return;
    }
    lastRefresh_ = now;
    freeBytes_ = platform::sdFreeBytes();
    wifi_ = platform::wifiStatus();
}

App::Action App::home()
{
    refreshStatus(true);
    while (ui::keepRunning()) {
        refreshStatus(false);
        const ui::Input input = ui::readInput();

        if (input.down & KEY_START) {
            return Action::Exit;
        }
        const int row = homeRow(homeSelection_);
        const int column = homeSelection_ < 2 ? 0 : (homeSelection_ - 2) % 2;
        if (input.repeat & KEY_UP) {
            homeSelection_ = homeButton((row + HomeRows - 1) % HomeRows,
                                        column);
        }
        if (input.repeat & KEY_DOWN) {
            homeSelection_ = homeButton((row + 1) % HomeRows, column);
        }
        if ((input.repeat & (KEY_LEFT | KEY_RIGHT)) && row >= 2) {
            homeSelection_ = homeButton(row, 1 - column);
        }

        int chosen = -1;
        if (input.down & KEY_A) {
            chosen = homeSelection_;
        }
        for (int index = 0; index < HomeButtonCount; ++index) {
            if (input.tapped(HomeButtons[index])) {
                chosen = homeSelection_ = index;
            }
        }
        switch (chosen) {
        case 0:
            return Action::Scan;
        case 1:
            return Action::EnterUrl;
        case 2:
            return Action::History;
        case 3:
            return Action::Files;
        case 4:
            return Action::Folder;
        case 5:
            return Action::Settings;
        default:
            break;
        }

        drawHome();
    }
    return Action::Exit;
}

void App::drawHome()
{
    ui::beginFrame();
    ui::drawHeader(wifi_ == platform::Wifi::Disconnected ? "no wi-fi"
                                                         : "ready");

    ui::drawPanel({18, 56, 364, 78});
    ui::drawText("DOWNLOAD FOLDER", 34, 65, 0.44f, ui::color::Muted);
    ui::drawText(ui::fitMiddle(settings_.destination, 0.66f, 330), 34, 84,
                 0.66f, ui::color::Text);
    ui::drawText(freeBytes_ >= 0
                     ? units::bytes(static_cast<uint64_t>(freeBytes_)) +
                           " free on the SD card"
                     : "Free space unknown",
                 34, 110, 0.46f, ui::color::Muted);

    ui::drawPanel({18, 146, 364, 70});
    switch (wifi_) {
    case platform::Wifi::Connected:
        ui::drawText("Wi-Fi connected", 34, 157, 0.58f, ui::color::Success);
        break;
    case platform::Wifi::Disconnected:
        ui::drawText("Not connected to Wi-Fi", 34, 157, 0.58f,
                     ui::color::Warning);
        break;
    case platform::Wifi::Unknown:
        ui::drawText("Wi-Fi status unknown", 34, 157, 0.58f,
                     ui::color::Muted);
        break;
    }
    ui::drawText("Scan a QR code or type a direct file link.", 34, 184,
                 0.46f, ui::color::Muted);
    ui::drawText("v" APP_VERSION, 382, 224, 0.4f, ui::color::Muted,
                 C2D_AlignRight);

    ui::beginBottom();
    ui::drawText("Choose an action", 18, 10, 0.46f, ui::color::Muted);
    ui::drawText("START: Exit", 302, 10, 0.46f, ui::color::Muted,
                 C2D_AlignRight);
    for (int index = 0; index < HomeButtonCount; ++index) {
        ui::drawButton(HomeButtons[index], HomeLabels[index],
                       index == homeSelection_);
    }
    ui::endFrame();
}

// --- Downloading -------------------------------------------------------------

void App::startDownload(const std::string &address, std::string folder)
{
    const std::string link = text::trim(address);
    if (link.empty()) {
        return;
    }
    if (!url::isHttpUrl(link)) {
        ui::message("Not a download link",
                    "3dsDL can only download direct http:// or https:// "
                    "links.",
                    ui::Tone::Error, link);
        return;
    }
    if (!confirmDownload(link, folder)) {
        return;
    }
    if (!fs::makeDirectories(folder)) {
        ui::message("Folder unavailable", "Could not create " + folder + ".",
                    ui::Tone::Error);
        return;
    }

    if (platform::wifiStatus() == platform::Wifi::Disconnected) {
        ui::Dialog dialog;
        dialog.header = "no wi-fi";
        dialog.title = "The console is not connected to Wi-Fi";
        dialog.tone = ui::Tone::Warning;
        dialog.body.push_back({"Turn wireless communication on, or check the "
                               "connection in System Settings.",
                               ui::color::Text, 0.5f, 3});
        dialog.buttons = {"TRY ANYWAY", "CANCEL"};
        dialog.initial = 1;
        dialog.cancelButton = 1;
        if (ui::choose(dialog) != 0) {
            return;
        }
    }

    while (ui::keepRunning()) {
        net::Request request;
        request.url = link;
        request.folder = folder;
        request.caFile = CaBundle;
        request.userAgent = UserAgent;
        request.allowResume = settings_.resumeDownloads;
        request.socketBufferSize = SocketBufferSize;
        request.connections = DownloadConnections;
        request.freeSpace = [](const std::string &) {
            return platform::sdFreeBytes();
        };

        const net::Result result = tasks::download(request,
                                                   settings_.keepAwake);
        if (!ui::keepRunning()) {
            if (result.resumable) {
                HistoryEntry entry =
                    newEntry(link, folder, HistoryStatus::Failed);
                entry.message = "Interrupted when 3dsDL closed.";
                entry.resumable = true;
                entry.bytes = result.bytes;
                record(entry);
            }
            return;
        }

        if (result.outcome == net::Outcome::Completed) {
            finishDownload(link, folder, result);
            return;
        }
        if (result.outcome == net::Outcome::Cancelled) {
            HistoryEntry entry =
                newEntry(link, folder, HistoryStatus::Cancelled);
            entry.message = result.message;
            record(entry);
            return;
        }

        HistoryEntry entry = newEntry(link, folder, HistoryStatus::Failed);
        entry.message = result.message;
        entry.resumable = result.resumable;
        entry.bytes = result.bytes;
        record(entry);

        ui::Dialog dialog;
        dialog.header = "error";
        dialog.title = "Download failed";
        dialog.tone = ui::Tone::Error;
        dialog.body.push_back({result.message, ui::color::Text, 0.5f, 4});
        if (result.resumable) {
            dialog.body.push_back({units::bytes(result.bytes) +
                                       " arrived before it stopped. Retrying "
                                       "continues from there.",
                                   ui::color::Success, 0.46f, 2});
        }
        if (result.certificateProblem) {
            dialog.body.push_back({"The console's clock says " +
                                       units::dateTime(platform::now()) + ".",
                                   ui::color::Warning, 0.46f, 1});
        }
        if (!result.detail.empty() && result.detail != result.message) {
            dialog.body.push_back({result.detail, ui::color::Muted, 0.4f, 2});
        }
        if (result.certificateProblem) {
            dialog.buttons = {"CHECK CLOCK", "RETRY", "CLOSE"};
            dialog.cancelButton = 2;
        } else {
            dialog.buttons = {"RETRY", "CLOSE"};
            dialog.cancelButton = 1;
        }
        const int retryButton = result.certificateProblem ? 1 : 0;

        bool retry = false;
        while (ui::keepRunning()) {
            const int choice = ui::choose(dialog);
            if (result.certificateProblem && choice == 0) {
                if (clockScreen()) {
                    retry = true;
                    break;
                }
                continue;
            }
            retry = choice == retryButton;
            break;
        }
        if (!retry) {
            return;
        }
        // The retry gets its own history entry.
        history_.erase(history_.begin());
    }
}

bool App::confirmDownload(const std::string &address, std::string &folder)
{
    while (ui::keepRunning()) {
        const uint64_t partial = settings_.resumeDownloads
            ? net::partialSize(folder, address)
            : 0;

        ui::Dialog dialog;
        dialog.header = "confirm";
        dialog.title = "Download this link?";
        dialog.body.push_back({url::host(address), ui::color::Text, 0.62f, 1});
        dialog.body.push_back({address, ui::color::Muted, 0.44f, 4});
        dialog.body.push_back({"Save to " + folder, ui::color::Text, 0.48f,
                               2});
        if (!url::isHttps(address)) {
            dialog.body.push_back({"This link is not encrypted (http), so "
                                   "the file could be altered on the way.",
                                   ui::color::Warning, 0.44f, 2});
        }
        if (partial > 0) {
            dialog.body.push_back({"Continues the earlier download from " +
                                       units::bytes(partial) + ".",
                                   ui::color::Success, 0.44f, 1});
        }
        dialog.buttons = {"DOWNLOAD", "FOLDER", "CANCEL"};
        dialog.cancelButton = 2;

        const int choice = ui::choose(dialog);
        if (choice == 0) {
            return true;
        }
        if (choice != 1) {
            return false;
        }
        std::string chosen = folder;
        if (chooseFolder(chosen)) {
            folder = chosen;
            settings_.destination = chosen;
            persistSettings();
        }
    }
    return false;
}

void App::finishDownload(const std::string &address, const std::string &folder,
                         const net::Result &result)
{
    bool overwrite = false;
    if (fs::fileExists(folder + result.filename)) {
        const std::string suggestion = basename(
            naming::unusedPath(folder, result.filename, fs::fileExists));

        ui::Dialog dialog;
        dialog.header = "file exists";
        dialog.title = "A file with this name already exists";
        dialog.tone = ui::Tone::Warning;
        dialog.body.push_back({result.filename, ui::color::Text, 0.56f, 2});
        dialog.body.push_back({"Rename saves the new download as " +
                                   suggestion + " and keeps the old file.",
                               ui::color::Muted, 0.46f, 3});
        dialog.body.push_back({"Overwrite replaces the old file.",
                               ui::color::Muted, 0.46f, 1});
        dialog.buttons = {"RENAME", "OVERWRITE", "CANCEL"};
        dialog.dangerous = 1;
        dialog.cancelButton = 2;

        const int choice = ui::choose(dialog);
        if (choice != 0 && choice != 1) {
            fs::removeFile(result.partPath);
            HistoryEntry entry =
                newEntry(address, folder, HistoryStatus::Cancelled);
            entry.message = "Kept the existing " + result.filename + ".";
            record(entry);
            return;
        }
        overwrite = choice == 1;
    }

    const finalize::Result saved =
        finalize::commit(result.partPath, folder, result.filename, overwrite,
                         platform::now());
    HistoryEntry entry = newEntry(address, folder, saved.ok
                                                       ? HistoryStatus::Saved
                                                       : HistoryStatus::Failed);
    // How fast it went, so speeds can be compared between consoles and
    // releases.
    std::string speed;
    if (result.transferred > 0 && result.seconds > 0.0) {
        speed = units::rate(static_cast<double>(result.transferred) /
                            result.seconds);
        if (result.connections > 1) {
            speed += " over " + std::to_string(result.connections) +
                     " connections";
        }
    }

    entry.bytes = result.bytes;
    entry.path = saved.path;
    entry.message = !saved.ok       ? saved.message
                  : speed.empty() ? "Saved"
                                  : "Saved at " + speed;
    record(entry);

    if (!saved.ok) {
        ui::message("Could not save the file", saved.message,
                    ui::Tone::Error);
        return;
    }

    const std::string summary = saved.usedFallbackName
        ? saved.message
        : "Saved as " + saved.path + " (" + units::bytes(result.bytes) +
              (speed.empty() ? "" : ", " + speed) + ").";
    if (cia::isCiaFile(saved.path)) {
        offerInstall(saved.path, address, summary);
        return;
    }
    if (settings_.offerZipExtraction &&
        text::endsWithNoCase(saved.path, ".zip")) {
        offerExtraction(saved.path, folder, summary);
        return;
    }
    ui::message("Download complete", summary, ui::Tone::Success);
}

void App::offerExtraction(const std::string &zipPath, const std::string &folder,
                          const std::string &summary, const std::string &title)
{
    zip::Archive archive;
    std::string error;
    if (!zip::readArchive(zipPath, archive, error)) {
        ui::message(summary.empty() ? "Cannot extract" : "Download complete",
                    summary.empty() ? error : summary,
                    summary.empty() ? ui::Tone::Error : ui::Tone::Success,
                    summary.empty() ? "" : "It was not extracted: " + error);
        return;
    }

    const std::string ownFolder =
        folder + naming::safeFilename(naming::stem(basename(zipPath))) + "/";

    ui::Dialog dialog;
    dialog.header = "extract";
    dialog.title = title;
    dialog.tone = ui::Tone::Success;
    dialog.body.push_back({summary, ui::color::Muted, 0.44f, 3});
    dialog.body.push_back({"It holds " + plural(archive.fileCount, "file") +
                               " (" + units::bytes(archive.totalSize) +
                               "). HERE extracts into " + folder +
                               ", NEW FOLDER into " + ownFolder + ".",
                           ui::color::Text, 0.46f, 5});
    dialog.buttons = {"HERE", "NEW FOLDER", "NO"};
    dialog.cancelButton = 2;
    const int choice = ui::choose(dialog);
    if (choice != 0 && choice != 1) {
        return;
    }
    const std::string target = choice == 0 ? folder : ownFolder;

    const int64_t available = platform::sdFreeBytes();
    if (available >= 0 &&
        static_cast<uint64_t>(available) < archive.totalSize) {
        ui::message("Not enough free space",
                    "Extracting needs " + units::bytes(archive.totalSize) +
                        ", but only " +
                        units::bytes(static_cast<uint64_t>(available)) +
                        " is free.",
                    ui::Tone::Error);
        return;
    }

    zip::Existing existing = zip::Existing::Skip;
    const std::vector<std::string> clashes =
        zip::existingFiles(archive, target);
    if (!clashes.empty()) {
        ui::Dialog question;
        question.header = "extract";
        question.title = plural(clashes.size(), "file") + " already exist";
        question.tone = ui::Tone::Warning;
        question.body.push_back({"For example " + clashes.front() + " in " +
                                     target,
                                 ui::color::Text, 0.46f, 3});
        question.buttons = {"OVERWRITE", "SKIP THEM", "CANCEL"};
        question.dangerous = 0;
        question.initial = 1;
        question.cancelButton = 2;
        const int answer = ui::choose(question);
        if (answer != 0 && answer != 1) {
            return;
        }
        existing = answer == 0 ? zip::Existing::Overwrite
                               : zip::Existing::Skip;
    }

    const zip::Result result = tasks::extract(zipPath, archive, target,
                                              existing, settings_.keepAwake);
    if (!ui::keepRunning()) {
        return;
    }
    const std::string extracted =
        plural(result.extracted, "file") + " extracted into " + target + ".";
    if (result.cancelled) {
        ui::message("Extraction stopped", extracted, ui::Tone::Warning);
        return;
    }
    if (!result.ok) {
        ui::message("Extraction failed", result.message, ui::Tone::Error,
                    extracted);
        return;
    }

    std::string notes;
    if (result.skippedExisting > 0) {
        notes += plural(result.skippedExisting, "existing file") +
                 " left alone. ";
    }
    if (result.skippedUnsafe > 0) {
        notes += plural(result.skippedUnsafe, "entry") +
                 " skipped because the path would leave the folder. ";
    }
    if (result.skippedUnsupported > 0) {
        notes += plural(result.skippedUnsupported, "entry") +
                 " skipped (encrypted or unsupported compression). ";
    }

    ui::Dialog done;
    done.header = "done";
    done.title = "Archive extracted";
    done.tone = ui::Tone::Success;
    done.body.push_back({extracted, ui::color::Text, 0.5f, 3});
    done.body.push_back({notes, ui::color::Muted, 0.44f, 4});
    done.buttons = {"DONE", "DELETE ZIP"};
    done.dangerous = 1;
    done.cancelButton = 0;
    if (ui::choose(done) == 1 && !fs::removeFile(zipPath)) {
        ui::message("Could not delete the archive", zipPath,
                    ui::Tone::Error);
    }
}

// --- Installing --------------------------------------------------------------

void App::offerInstall(const std::string &path, const std::string &address,
                       const std::string &summary)
{
    cia::Info info;
    std::string error;
    if (!cia::readInfo(path, info, error)) {
        ui::message("Download complete", summary, ui::Tone::Success,
                    "It looks like a CIA but cannot be installed: " + error);
        return;
    }
    if (!installer::available()) {
        ui::message("Download complete", summary, ui::Tone::Success,
                    installer::unavailableReason());
        return;
    }

    CiaAction action = settings_.afterCiaDownload;
    if (action == CiaAction::Keep) {
        ui::message("Download complete", summary, ui::Tone::Success,
                    "Install it from Files on the home screen.");
        return;
    }
    if (action == CiaAction::Ask) {
        ui::Dialog dialog;
        dialog.header = "install";
        dialog.title = "Download complete. Install " + ciaName(info) + "?";
        dialog.tone = ui::Tone::Success;
        dialog.icon = ui::showIcon(info.smdh.icon);
        addCiaParagraphs(dialog, info);
        dialog.body.push_back({summary, ui::color::Muted, 0.42f, 2});
        dialog.buttons = {"INSTALL", "INSTALL & DELETE", "KEEP FILE"};
        dialog.cancelButton = 2;
        const int choice = ui::choose(dialog);
        if (choice != 0 && choice != 1) {
            return;
        }
        action = choice == 0 ? CiaAction::Install
                             : CiaAction::InstallAndDelete;
    }
    installCia(path, info, action == CiaAction::InstallAndDelete, address);
}

bool App::installCia(const std::string &path, const cia::Info &info,
                     bool deleteAfter, const std::string &address, bool quiet)
{
    const std::string name = ciaName(info);
    if (!installer::available()) {
        ui::message("Cannot install", installer::unavailableReason(),
                    ui::Tone::Error);
        return false;
    }
    if (cia::isSystemTitle(info.titleId)) {
        ui::message("Not installed",
                    "This is a system title. A broken system title can stop "
                    "the console from starting, so 3dsDL does not install "
                    "them.",
                    ui::Tone::Error, cia::titleIdText(info.titleId));
        return false;
    }

    bool isNew3ds = false;
    APT_CheckNew3DS(&isNew3ds);
    if (cia::isNew3dsOnly(info.titleId) && !isNew3ds) {
        ui::Dialog dialog;
        dialog.header = "install";
        dialog.title = name + " is made for the New 3DS";
        dialog.tone = ui::Tone::Warning;
        dialog.icon = ui::showIcon(info.smdh.icon);
        dialog.body.push_back({"It will install, but will not start on this "
                               "console.",
                               ui::color::Text, 0.5f, 2});
        dialog.buttons = {"INSTALL ANYWAY", "CANCEL"};
        dialog.initial = 1;
        dialog.cancelButton = 1;
        if (ui::choose(dialog) != 0) {
            return false;
        }
    }

    // AM refuses to go back to an older version, so that takes removing the
    // installed one first, which can take its save data with it.
    uint16_t installed = 0;
    const bool isInstalled = installer::installedVersion(info.titleId,
                                                         installed);
    const auto confirmReplace = [&](const std::string &title) {
        ui::Dialog dialog;
        dialog.header = "install";
        dialog.title = title;
        dialog.tone = ui::Tone::Warning;
        dialog.icon = ui::showIcon(info.smdh.icon);
        dialog.body.push_back({"Installed: " + cia::versionText(installed) +
                                   "\nThis file: " +
                                   cia::versionText(info.version),
                               ui::color::Text, 0.48f, 2});
        dialog.body.push_back({"Replacing it removes the installed copy "
                               "first, and its save data may go with it. "
                               "Back the save up with Checkpoint before "
                               "you replace it.",
                               ui::color::Warning, 0.46f, 4});
        dialog.buttons = {"REPLACE", "CANCEL"};
        dialog.dangerous = 0;
        dialog.initial = 1;
        dialog.cancelButton = 1;
        return ui::choose(dialog) == 0;
    };
    bool removeFirst = false;
    if (isInstalled && installed > info.version) {
        if (!confirmReplace("A newer version of " + name +
                            " is installed")) {
            return false;
        }
        removeFirst = true;
    }

    if (installer::destination(info.titleId) == MEDIATYPE_SD) {
        const int64_t needed = installer::requiredSpace(path, info.titleId);
        const int64_t free = platform::sdFreeBytes();
        if (needed > 0 && free >= 0 && needed > free) {
            ui::message("Not enough space",
                        name + " needs " +
                            units::bytes(static_cast<uint64_t>(needed)) +
                            " on the SD card and " +
                            units::bytes(static_cast<uint64_t>(free)) +
                            " is free.",
                        ui::Tone::Error);
            return false;
        }
    }

    installer::Result result;
    while (ui::keepRunning()) {
        if (removeFirst) {
            std::string error;
            if (!installer::removeTitle(info.titleId, error)) {
                ui::message("Could not replace " + name, error,
                            ui::Tone::Error);
                return false;
            }
        }
        result = tasks::install(path, info.titleId, name,
                                settings_.keepAwake);
        if (result.outcome == installer::Outcome::Failed &&
            result.versionRefused && isInstalled && !removeFirst &&
            ui::keepRunning() &&
            confirmReplace(name + " is already installed")) {
            removeFirst = true;
            continue;
        }
        break;
    }

    if (result.outcome == installer::Outcome::Cancelled) {
        if (ui::keepRunning()) {
            ui::message("Install cancelled", result.message);
        }
        return false;
    }
    if (result.outcome != installer::Outcome::Installed) {
        if (!address.empty()) {
            markInstalled(address, path, "Install failed: " + result.message);
        }
        ui::message("Install failed", result.message, ui::Tone::Error,
                    name);
        return false;
    }

    std::vector<ui::Paragraph> notes;
    if (info.seed != cia::Seed::NotNeeded) {
        std::string seedMessage;
        bool added = false;
        tasks::busy("installing", "Getting the seed " + name + " needs...",
                    [&]() {
                        added = installer::installSeed(info.titleId,
                                                       seedMessage);
                    });
        // An encrypted CIA may not need a seed at all, so a missing one is
        // only worth mentioning when the game is known to need it.
        if (added || info.seed == cia::Seed::Needed) {
            notes.push_back({seedMessage,
                             added ? ui::color::Muted : ui::color::Warning,
                             0.44f, 3});
        }
    }

    std::string where = "The CIA file is still at " + path + ".";
    if (deleteAfter) {
        where = fs::removeFile(path) ? "The CIA file was deleted."
                                     : "The CIA file could not be deleted.";
    }
    notes.push_back({where, ui::color::Muted, 0.42f, 2});
    if (!address.empty()) {
        markInstalled(address, path,
                      deleteAfter ? "Installed, file deleted" : "Installed");
    }

    if (quiet) {
        return true;
    }

    // An update or DLC opens with its game, when that is installed.
    const uint64_t launchId = cia::baseTitle(info.titleId);
    uint16_t unused = 0;
    const cia::Kind kind = cia::kind(launchId);
    const bool canLaunch =
        (kind == cia::Kind::Application || kind == cia::Kind::Demo) &&
        (launchId == info.titleId ||
         installer::installedVersion(launchId, unused));

    ui::Dialog dialog;
    dialog.header = "installed";
    dialog.title = name + " is installed";
    dialog.tone = ui::Tone::Success;
    dialog.icon = ui::showIcon(info.smdh.icon);
    dialog.body.push_back(
        {std::string(cia::kindName(cia::kind(info.titleId))) + ", " +
             cia::versionText(info.version) + ", " +
             units::bytes(result.bytes) +
             (result.seconds > 0.5
                  ? " in " + units::duration(static_cast<int64_t>(
                                 result.seconds + 0.5))
                  : ""),
         ui::color::Text, 0.46f, 2});
    if (kind == cia::Kind::Application || kind == cia::Kind::Demo) {
        dialog.body.push_back({"Find it on the HOME Menu.", ui::color::Text,
                               0.46f, 1});
    }
    dialog.body.insert(dialog.body.end(), notes.begin(), notes.end());
    if (canLaunch) {
        dialog.buttons = {"LAUNCH", "CLOSE"};
        dialog.initial = 1;
        dialog.cancelButton = 1;
    } else {
        dialog.buttons = {"CLOSE"};
    }
    if (ui::choose(dialog) == 0 && canLaunch) {
        std::string error;
        if (!installer::launch(launchId, error)) {
            ui::message("Could not launch", error, ui::Tone::Error);
        }
    }
    return true;
}

void App::markInstalled(const std::string &address, const std::string &path,
                        const std::string &message)
{
    for (HistoryEntry &entry : history_) {
        if (entry.url == address && entry.path == path) {
            if (message.compare(0, 9, "Installed") == 0) {
                entry.status = HistoryStatus::Installed;
            }
            entry.message = message;
            storage::saveHistory(history_);
            return;
        }
    }
}

// --- Folders -----------------------------------------------------------------

void App::changeDefaultFolder()
{
    std::string folder = settings_.destination;
    if (chooseFolder(folder)) {
        settings_.destination = folder;
        persistSettings();
    }
}

bool App::chooseFolder(std::string &folder)
{
    int cursor = 2;
    while (ui::keepRunning()) {
        std::vector<std::string> paths = {"", ""};
        ui::ListSpec spec;
        spec.header = "download folder";
        spec.title = "Where should downloads go?";
        spec.hint = "A: Choose    B: Back";
        spec.items.push_back({"Browse the SD card...", "", ui::color::Primary});
        spec.items.push_back({"Type a folder path...", "", ui::color::Primary});

        for (const Preset &preset : Presets) {
            spec.items.push_back(
                {preset.label, fs::directoryExists(preset.path) ? "" : "new"});
            paths.push_back(preset.path);
        }
        for (const HistoryEntry &entry : history_) {
            if (paths.size() >= 2 + std::size(Presets) + MaxRecentFolders) {
                break;
            }
            if (std::find(paths.begin(), paths.end(), entry.folder) ==
                    paths.end() &&
                fs::directoryExists(entry.folder)) {
                spec.items.push_back({"Recent: " + entry.folder, ""});
                paths.push_back(entry.folder);
            }
        }

        spec.drawTop = [&](int index) {
            drawLabelledPanel(56, 62, "CURRENT FOLDER", folder,
                              ui::color::Text);
            std::string label = "SELECTED";
            std::string value;
            u32 valueColor = ui::color::Text;
            if (index == 0) {
                value = "Pick any folder on the SD card, or make a new one.";
            } else if (index == 1) {
                value = "Type a path that starts with sdmc:/.";
            } else if (index >= 2) {
                value = paths[index];
                if (!fs::directoryExists(value)) {
                    label = "SELECTED (WILL BE CREATED)";
                    valueColor = ui::color::Warning;
                }
            }
            drawLabelledPanel(132, 88, label, value, valueColor);
        };

        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key != KEY_A) {
            return false;
        }
        std::string chosen = folder;
        if (result.index == 0) {
            if (browseFolders(chosen)) {
                folder = chosen;
                return true;
            }
            continue;
        }
        if (result.index == 1) {
            if (typeFolder(chosen)) {
                folder = chosen;
                return true;
            }
            continue;
        }
        const std::string &path = paths[result.index];
        if (!fs::makeDirectories(path)) {
            ui::message("Folder unavailable", "Could not create " + path + ".",
                        ui::Tone::Error);
            continue;
        }
        folder = path;
        return true;
    }
    return false;
}

bool App::browseFolders(std::string &folder)
{
    std::string current = fs::directoryExists(folder)
        ? naming::normaliseFolder(folder)
        : "sdmc:/";
    int cursor = 0;

    while (ui::keepRunning()) {
        const std::vector<std::string> names = fs::listFolders(current);
        const bool atRoot = naming::parentFolder(current) == current;
        const int firstFolder = atRoot ? 1 : 2;

        ui::ListSpec spec;
        spec.header = "browse";
        spec.title = ui::fitMiddle(current, 0.56f, 290);
        spec.hint = "A: Open  B: Up  X: New folder  Y: Use this";
        spec.extraKeys = KEY_X | KEY_Y;
        spec.items.push_back({"Use this folder", "", ui::color::Success});
        if (!atRoot) {
            spec.items.push_back({".. (up one level)", "", ui::color::Muted});
        }
        for (const std::string &name : names) {
            spec.items.push_back({name + "/", ""});
        }
        spec.drawTop = [&](int index) {
            drawLabelledPanel(56, 74, "BROWSING", current, ui::color::Text);
            std::string value = plural(names.size(), "subfolder") + ".";
            if (index >= firstFolder) {
                value = current + names[index - firstFolder] + "/";
            }
            drawLabelledPanel(144, 76, "SELECTED", value, ui::color::Text);
        };

        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key == 0) {
            return false;
        }
        if (result.key == KEY_Y || (result.key == KEY_A && result.index == 0)) {
            folder = current;
            return true;
        }
        if (result.key == KEY_B || (result.key == KEY_A && !atRoot &&
                                    result.index == 1)) {
            if (atRoot) {
                return false;
            }
            current = naming::parentFolder(current);
            cursor = 0;
            continue;
        }
        if (result.key == KEY_X) {
            std::string name;
            if (platform::keyboard(platform::KeyboardKind::Name,
                                   "New folder name", "", name)) {
                const std::string created =
                    current + naming::safeFilename(text::trim(name)) + "/";
                if (fs::makeDirectories(created)) {
                    current = created;
                    cursor = 0;
                } else {
                    ui::message("Could not create the folder", created,
                                ui::Tone::Error);
                }
            }
            continue;
        }
        if (result.index >= firstFolder) {
            current += names[result.index - firstFolder] + "/";
            cursor = 0;
        }
    }
    return false;
}

bool App::typeFolder(std::string &folder)
{
    std::string typed;
    if (!platform::keyboard(platform::KeyboardKind::Path,
                            "Folder, for example sdmc:/downloads/", folder,
                            typed)) {
        return false;
    }

    typed = naming::normaliseFolder(typed);
    if (!naming::isSdPath(typed) || typed.find("/../") != std::string::npos ||
        typed.find("/./") != std::string::npos) {
        ui::message("Not an SD card folder",
                    "Folders must start with sdmc:/ and cannot contain . or "
                    ".. parts.",
                    ui::Tone::Error, typed);
        return false;
    }
    typed.replace(0, 5, "sdmc:");

    if (!fs::directoryExists(typed)) {
        ui::Dialog dialog;
        dialog.header = "new folder";
        dialog.title = "Create this folder?";
        dialog.body.push_back({typed, ui::color::Text, 0.52f, 3});
        dialog.buttons = {"CREATE", "CANCEL"};
        dialog.cancelButton = 1;
        if (ui::choose(dialog) != 0) {
            return false;
        }
        if (!fs::makeDirectories(typed)) {
            ui::message("Could not create the folder", typed,
                        ui::Tone::Error);
            return false;
        }
    }
    folder = typed;
    return true;
}

// --- History -----------------------------------------------------------------

bool App::confirmClearHistory()
{
    ui::Dialog dialog;
    dialog.header = "history";
    dialog.title = "Clear the download history?";
    dialog.tone = ui::Tone::Warning;
    dialog.body.push_back({"Downloaded files stay on the SD card. Partial "
                           "downloads kept for resuming are deleted.",
                           ui::color::Text, 0.48f, 3});
    dialog.buttons = {"CLEAR", "CANCEL"};
    dialog.dangerous = 0;
    dialog.initial = 1;
    dialog.cancelButton = 1;
    if (ui::choose(dialog) != 0) {
        return false;
    }
    for (const HistoryEntry &entry : history_) {
        if (entry.resumable) {
            net::discardPartial(entry.folder, entry.url);
        }
    }
    history_.clear();
    storage::saveHistory(history_);
    return true;
}

void App::historyScreen()
{
    int cursor = 0;
    while (ui::keepRunning()) {
        ui::ListSpec spec;
        spec.header = "history";
        spec.title = "Download history (" + std::to_string(history_.size()) +
                     ")";
        spec.emptyText = "Nothing here yet. Finished, failed and cancelled "
                         "downloads are listed here.";
        spec.hint = history_.empty() ? "B: Back"
                                     : "A: Options  X: Clear all  B: Back";
        spec.extraKeys = KEY_X;
        for (const HistoryEntry &entry : history_) {
            std::string detail;
            if (entry.status == HistoryStatus::Saved ||
                entry.status == HistoryStatus::Installed) {
                detail = units::bytes(entry.bytes);
            } else if (entry.resumable &&
                       net::partialSize(entry.folder, entry.url) > 0) {
                detail = "can resume";
            } else {
                detail = text::lower(statusLabel(entry.status));
            }
            spec.items.push_back(
                {entryName(entry), detail, statusColor(entry.status)});
        }

        spec.drawTop = [&](int index) {
            if (index < 0) {
                drawLabelledPanel(56, 80, "HISTORY",
                                  "Your downloads will appear here.",
                                  ui::color::Muted);
                return;
            }
            const HistoryEntry &entry = history_[index];
            ui::drawPanel({18, 54, 364, 172});
            ui::drawText(statusLabel(entry.status), 34, 63, 0.6f,
                         entry.status == HistoryStatus::Saved
                             ? ui::color::Success
                             : statusColor(entry.status));
            ui::drawText(units::dateTime(entry.time), 366, 66, 0.44f,
                         ui::color::Muted, C2D_AlignRight);
            float y = ui::drawWrapped(entry.url, 34, 88, 0.44f,
                                      ui::color::Muted, 330, 3) + 6;
            const std::string where =
                entry.status == HistoryStatus::Saved ? entry.path
                : entry.status == HistoryStatus::Installed
                    ? entry.message + ": " + entry.path
                    : entry.message;
            y = ui::drawWrapped(where, 34, y, 0.48f, ui::color::Text, 330, 3) +
                6;
            const uint64_t partial =
                entry.resumable ? net::partialSize(entry.folder, entry.url)
                                : 0;
            if (partial > 0) {
                ui::drawWrapped(units::bytes(partial) +
                                    " is kept; downloading again resumes it.",
                                34, y, 0.44f, ui::color::Success, 330, 2);
            } else {
                ui::drawWrapped("Folder: " + entry.folder, 34, y, 0.44f,
                                ui::color::Muted, 330, 2);
            }
        };

        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key == KEY_X) {
            if (!history_.empty()) {
                confirmClearHistory();
            }
            continue;
        }
        if (result.key != KEY_A || result.index < 0 ||
            result.index >= static_cast<int>(history_.size())) {
            return;
        }

        const HistoryEntry entry = history_[result.index];
        const bool canResume =
            entry.resumable && net::partialSize(entry.folder, entry.url) > 0;
        ui::Dialog dialog;
        dialog.header = "history";
        dialog.title = entryName(entry);
        dialog.body.push_back({entry.url, ui::color::Muted, 0.44f, 4});
        dialog.body.push_back({"Folder: " + entry.folder, ui::color::Text,
                               0.46f, 2});
        // A CIA that is still on the SD card can be installed from here.
        cia::Info info;
        std::string error;
        const bool installable = !entry.path.empty() &&
                                 cia::isCiaFile(entry.path) &&
                                 cia::readInfo(entry.path, info, error);
        if (installable) {
            dialog.icon = ui::showIcon(info.smdh.icon);
            dialog.buttons = {"INSTALL", "DOWNLOAD", "REMOVE"};
            dialog.dangerous = 2;
        } else {
            dialog.buttons = {canResume ? "RESUME" : "DOWNLOAD", "REMOVE",
                              "BACK"};
            dialog.dangerous = 1;
            dialog.cancelButton = 2;
        }
        int choice = ui::choose(dialog);
        if (installable) {
            if (choice == 0) {
                installCia(entry.path, info, false, entry.url);
                continue;
            }
            // The other two buttons work as below.
            choice = choice < 0 ? -1 : choice - 1;
        }
        if (choice == 0) {
            startDownload(entry.url, fs::directoryExists(entry.folder)
                                         ? entry.folder
                                         : settings_.destination);
            cursor = 0;
        } else if (choice == 1) {
            if (entry.resumable) {
                net::discardPartial(entry.folder, entry.url);
            }
            history_.erase(history_.begin() + result.index);
            storage::saveHistory(history_);
        }
    }
}

void App::record(HistoryEntry entry)
{
    entry.time = platform::now();

    // Older entries for the same download share its partial file, which now
    // belongs to this entry (or no longer exists).
    for (HistoryEntry &older : history_) {
        if (older.url == entry.url && older.folder == entry.folder) {
            older.resumable = false;
        }
    }
    // The oldest entry is about to drop off; do not orphan its partial file.
    if (history_.size() >= HistoryLimit && history_.back().resumable) {
        net::discardPartial(history_.back().folder, history_.back().url);
    }

    addHistoryEntry(history_, entry);
    storage::saveHistory(history_);
}

// --- Settings ----------------------------------------------------------------

void App::persistSettings()
{
    if (!storage::saveSettings(settings_)) {
        ui::message("Settings not saved",
                    "The change works for now, but could not be written to "
                    "the SD card.",
                    ui::Tone::Warning);
    }
}

void App::settingsScreen()
{
    static const char *const Descriptions[] = {
        "When a download is interrupted, keep what arrived and continue from "
        "there next time. 3dsDL only resumes when the server confirms the "
        "file has not changed.",
        "Pause sleep mode while a download or extraction runs. Closing the "
        "lid would otherwise turn Wi-Fi off and stop the transfer.",
        "After a .zip file finishes downloading, ask whether to extract it. "
        "Nothing is ever extracted without asking.",
        "What happens when a CIA finishes downloading: ask each time, "
        "install it and keep the file, install it and delete the file, or "
        "only keep the file.",
        "The folder new downloads are saved to.",
        "Compare the console's clock with the time online and set it for "
        "your time zone. HTTPS downloads fail when the date is wrong.",
        "Removes the list of past downloads and any partial downloads kept "
        "for resuming.",
        "Version, credits and where 3dsDL keeps its files.",
    };

    int cursor = 0;
    while (ui::keepRunning()) {
        ui::ListSpec spec;
        spec.header = "settings";
        spec.title = "Settings";
        spec.hint = "A: Change    B: Back";
        spec.items = {
            {"Resume interrupted downloads", onOff(settings_.resumeDownloads)},
            {"Stay awake while downloading", onOff(settings_.keepAwake)},
            {"Offer to extract ZIP files", onOff(settings_.offerZipExtraction)},
            {"After downloading a CIA", ciaActionLabel(settings_.afterCiaDownload)},
            {"Default download folder", ""},
            {"Console clock", units::dateTime(platform::now())},
            {"Clear download history", std::to_string(history_.size())},
            {"About 3dsDL", "v" APP_VERSION},
        };
        spec.drawTop = [&](int index) {
            if (index < 0) {
                return;
            }
            const std::string value =
                index == 4 ? settings_.destination
                           : std::string(Descriptions[index]);
            drawLabelledPanel(56, 120, text::lower(spec.items[index].label),
                              index == 4 ? std::string(Descriptions[4]) +
                                               " Currently " + value
                                         : value,
                              ui::color::Text);
        };

        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key != KEY_A) {
            return;
        }
        switch (result.index) {
        case 0:
            settings_.resumeDownloads = !settings_.resumeDownloads;
            persistSettings();
            break;
        case 1:
            settings_.keepAwake = !settings_.keepAwake;
            persistSettings();
            break;
        case 2:
            settings_.offerZipExtraction = !settings_.offerZipExtraction;
            persistSettings();
            break;
        case 3:
            settings_.afterCiaDownload =
                settings_.afterCiaDownload == CiaAction::Ask
                    ? CiaAction::Install
                : settings_.afterCiaDownload == CiaAction::Install
                    ? CiaAction::InstallAndDelete
                : settings_.afterCiaDownload == CiaAction::InstallAndDelete
                    ? CiaAction::Keep
                    : CiaAction::Ask;
            persistSettings();
            break;
        case 4:
            changeDefaultFolder();
            break;
        case 5:
            clockScreen();
            break;
        case 6:
            if (history_.empty()) {
                ui::message("History is empty", "There is nothing to clear.");
            } else {
                confirmClearHistory();
            }
            break;
        case 7:
            aboutScreen();
            break;
        default:
            break;
        }
    }
}

// --- Clock -------------------------------------------------------------------

bool App::chooseTimeZone(int64_t utcNowSeconds)
{
    const std::vector<tz::Zone> &zones = tz::zones();
    int cursor = settings_.timeZoneSet ? tz::zoneIndex(settings_.timeZoneMinutes)
                                       : tz::zoneIndex(0);

    const u64 openedAt = osGetTime();
    ui::ListSpec spec;
    spec.header = "time zone";
    spec.title = "Your time zone";
    spec.hint = "A: Choose    B: Back";
    for (const tz::Zone &zone : zones) {
        spec.items.push_back(
            {tz::formatOffset(zone.offsetMinutes) + "  " + zone.places, ""});
    }
    spec.drawTop = [&](int index) {
        if (index < 0) {
            return;
        }
        const tz::Zone &zone = zones[index];
        const std::string value =
            utcNowSeconds >= 0
                ? "The time there now: " +
                      units::dateTime(
                          utcNowSeconds + zone.offsetMinutes * 60 +
                          static_cast<int64_t>((osGetTime() - openedAt) /
                                               1000))
                : std::string(zone.places);
        drawLabelledPanel(56, 76, tz::formatOffset(zone.offsetMinutes), value,
                          ui::color::Text);
        drawLabelledPanel(144, 76, "TIP",
                          "Pick the offset in use right now. Summer time "
                          "usually adds one hour.",
                          ui::color::Muted);
    };

    const ui::ListResult result = ui::runList(spec, cursor);
    if (result.key != KEY_A || result.index < 0) {
        return false;
    }
    settings_.timeZoneSet = true;
    settings_.timeZoneMinutes = zones[result.index].offsetMinutes;
    persistSettings();
    return true;
}

bool App::clockScreen()
{
    while (ui::keepRunning()) {
        ntp::Result network;
        tasks::busy("clock", "Checking the time online...", [&]() {
            network = ntp::queryAny(ntp::defaultServers());
        });
        const u64 answeredAt = osGetTime();
        if (!ui::keepRunning()) {
            return false;
        }

        if (!network.ok) {
            ui::Dialog dialog;
            dialog.header = "clock";
            dialog.title = "Could not get the time online";
            dialog.tone = ui::Tone::Error;
            dialog.body.push_back({network.error, ui::color::Text, 0.5f, 3});
            dialog.body.push_back({"Check the Wi-Fi connection. Some networks "
                                   "also block time servers (UDP port 123).",
                                   ui::color::Muted, 0.44f, 3});
            dialog.buttons = {"TRY AGAIN", "BACK"};
            dialog.cancelButton = 1;
            if (ui::choose(dialog) != 0) {
                return false;
            }
            continue;
        }

        const int64_t utcAtAnswer = network.unixMilliseconds;
        const auto utcNowMs = [&]() {
            return utcAtAnswer + static_cast<int64_t>(osGetTime() - answeredAt);
        };

        if (!settings_.timeZoneSet) {
            int guess = 0;
            if (tz::guessOffset(platform::now(), utcNowMs() / 1000, guess)) {
                settings_.timeZoneSet = true;
                settings_.timeZoneMinutes = guess;
                persistSettings();
            } else if (!chooseTimeZone(utcNowMs() / 1000)) {
                return false;
            }
        }

        while (ui::keepRunning()) {
            const int offset = settings_.timeZoneMinutes;
            const int64_t correctMs = utcNowMs() + offset * 60000LL;
            const int64_t console = platform::now();
            const int64_t correct = correctMs / 1000;
            const bool wrong = std::llabs(console - correct) >= 60;
            const int zone = tz::zoneIndex(offset);

            ui::Dialog dialog;
            dialog.header = "clock";
            dialog.title = wrong ? "The console clock is off"
                                 : "The console clock is right";
            dialog.tone = wrong ? ui::Tone::Warning : ui::Tone::Success;
            dialog.body.push_back({"Console:  " + units::dateTime(console),
                                   ui::color::Text, 0.52f, 1});
            dialog.body.push_back({"Actual:   " + units::dateTime(correct),
                                   ui::color::Success, 0.52f, 1});
            dialog.body.push_back({"The console clock is " +
                                       tz::describeDrift(console, correct) +
                                       ".",
                                   wrong ? ui::color::Warning
                                         : ui::color::Muted,
                                   0.46f, 2});
            dialog.body.push_back(
                {"Time zone " + tz::formatOffset(offset) +
                     (zone >= 0 ? std::string(", ") + tz::zones()[zone].places
                                : std::string()) +
                     ". Time from " + network.server + ".",
                 ui::color::Muted, 0.42f, 2});
            dialog.buttons = {"SET CLOCK", "TIME ZONE", "BACK"};
            dialog.initial = wrong ? 0 : 2;
            dialog.cancelButton = 2;

            const int choice = ui::choose(dialog);
            if (choice == 1) {
                chooseTimeZone(utcNowMs() / 1000);
                continue;
            }
            if (choice != 0) {
                return false;
            }

            std::string error;
            const int64_t targetMs = utcNowMs() + offset * 60000LL;
            if (!platform::setConsoleClock(targetMs, error)) {
                ui::message("Could not set the clock", error,
                            ui::Tone::Error);
                continue;
            }
            ui::message("Clock set",
                        "The console clock now reads " +
                            units::dateTime(targetMs / 1000) + " (" +
                            tz::formatOffset(offset) +
                            "). HTTPS downloads should work again.",
                        ui::Tone::Success);
            return true;
        }
    }
    return false;
}

void App::aboutScreen()
{
    ui::message("3dsDL " APP_VERSION,
                "Download files straight onto your Nintendo 3DS from a link "
                "or a QR code. Settings and history live in " +
                    std::string(storage::DataFolder) + ".",
                ui::Tone::Info,
                "GPLv3. Built on libcurl, Mbed TLS, quirc and Citro2D, and "
                "grew out of Multidownload by hax0kartik. CIA installing "
                "follows FBI by Steveice10 (MIT).");
}
