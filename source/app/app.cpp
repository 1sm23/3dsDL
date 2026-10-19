#include "app.hpp"

#include "app_util.hpp"
#include "cia.hpp"
#include "finalize.hpp"
#include "fs_util.hpp"
#include "installer.hpp"
#include "naming.hpp"
#include "ntp.hpp"
#include "platform.hpp"
#include "savedata.hpp"
#include "saves.hpp"
#include "scanner.hpp"
#include "sound.hpp"
#include "storage.hpp"
#include "tasks.hpp"
#include "text.hpp"
#include "timezone.hpp"
#include "ui.hpp"
#include "units.hpp"
#include "updater.hpp"
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

using http::CaBundle;
using http::DownloadConnections;
using http::SocketBufferSize;
using http::UserAgent;
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
        return "Install, Delete";
    case CiaAction::Keep:
        return "Keep File";
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
    if (!entry.name.empty()) {
        return entry.name;
    }
    const std::string fromUrl = naming::chooseFilename("", entry.url);
    return fromUrl == naming::DefaultFilename ? url::host(entry.url)
                                              : fromUrl;
}

// Before a title is removed to put another copy in its place, its save goes
// to Checkpoint's folder, the way Restore keeps what was there: removing a
// title can take its save with it. False when the person would rather stop.
// note says where the backup went, for the end of the install.
bool saveBeforeRemoving(const cia::Info &info, const std::string &name,
                        uint16_t installedVersion, std::string &note)
{
    titles::Title title;
    title.id = info.titleId;
    title.place = installer::destination(info.titleId) == MEDIATYPE_NAND
                      ? titles::Place::SystemMemory
                      : titles::Place::SdCard;
    title.version = installedVersion;
    title.name = name;
    title.englishShortName = !info.smdh.englishShortName.empty()
                                 ? info.smdh.englishShortName
                                 : info.smdh.shortName;
    if (!savedata::unsupportedReason(title).empty()) {
        return true; // nothing 3dsDL can keep, such as DSiWare
    }
    const savedata::Target target =
        savedata::saveOf(title, installer::destination(info.titleId));
    if (!savedata::exists(target)) {
        return true; // no save yet
    }
    const time_t seconds = static_cast<time_t>(platform::now());
    std::tm parts {};
    gmtime_r(&seconds, &parts); // the console clock is already local time
    const std::string backup = saves::backupName(parts) + " Before Reinstall";
    savedata::Progress progress;
    savedata::Result saved;
    tasks::busy("install", "Backing up the save of " + name + "...",
                [&]() { saved = savedata::backup(target, backup, progress); });
    if (saved.ok) {
        note = "The save was backed up first, as " + backup +
               " in Titles, Save Data, should it be needed.";
        return true;
    }
    ui::Dialog dialog;
    dialog.header = "install";
    dialog.title = "The save of " + name + " could not be backed up.";
    dialog.tone = ui::Tone::Warning;
    dialog.icon = ui::showIcon(info.smdh.icon);
    dialog.body.push_back({saved.message, ui::color::Text, 0.48f, 3});
    dialog.body.push_back({"Removing the installed copy may take the save "
                           "with it.",
                           ui::color::Warning, 0.46f, 2});
    dialog.buttons = {"Continue Anyway", "Cancel"};
    dialog.dangerous = 0;
    dialog.initial = 1;
    dialog.cancelButton = 1;
    return ui::choose(dialog) == 0;
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
    sound::setEnabled(settings_.sounds);
    // Interrupted downloads from before names were kept: their resume file
    // knows the name the server gave.
    for (HistoryEntry &entry : history_) {
        if (entry.name.empty() && entry.path.empty() && entry.resumable) {
            std::string contents;
            ResumeInfo info;
            if (fs::readFile(partialPaths(entry.folder, entry.url).info,
                             contents) &&
                parseResumeInfo(contents, info)) {
                entry.name = info.filename;
            }
        }
    }
    ui::setFrameHook([this]() { pollUpdate(); });
    if (settings_.checkForUpdates) {
        updater::startCheck();
    }
}

void App::run()
{
    while (ui::keepRunning() && !quit_) {
        switch (home()) {
        case Action::Scan: {
            // A code added to the queue goes straight back to the camera,
            // so several can be scanned in a row.
            bool typeInstead = false;
            while (ui::keepRunning()) {
                typeInstead = false;
                const std::string link = scanner::scan(&typeInstead);
                if (link.empty() ||
                    startDownload(link, settings_.destination) != Start::Queued) {
                    break;
                }
            }
            if (!typeInstead) {
                break;
            }
        }
            [[fallthrough]];
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
        case Action::Titles:
            titlesScreen();
            break;
        case Action::Queue:
            queueScreen();
            break;
        case Action::Receive:
            receiveScreen();
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
    ui::ScreenSpec spec;
    spec.closeBar = true;
    const auto add = [&](ui::Rect rect, const char *label, ui::Icon icon,
                         u32 iconColor, bool tile) {
        ui::Control control;
        control.rect = rect;
        control.label = label;
        control.icon = icon;
        control.iconColor = iconColor;
        control.tile = tile;
        control.scale = ui::type::Small + 0.06f;
        spec.controls.push_back(control);
    };
    add({16, 6, 140, 26}, "Settings", ui::Icon::Wrench, ui::color::IconGrey,
        false);
    add({164, 6, 140, 26}, "Queue", ui::Icon::Download, ui::color::IconGreen,
        false);
    if (!queue_.empty()) {
        spec.controls.back().badge = std::to_string(queue_.size());
    }
    // Three across: getting links in on top, finding things below.
    add({16, 42, 92, 78}, "Scan QR Code", ui::Icon::Scan, ui::color::IconBlue,
        true);
    add({114, 42, 92, 78}, "Enter URL", ui::Icon::Globe, ui::color::IconTeal,
        true);
    add({212, 42, 92, 78}, "Receive", ui::Icon::Chip, ui::color::IconPink,
        true);
    add({16, 127, 92, 76}, "History", ui::Icon::History, ui::color::IconRed,
        true);
    add({114, 127, 92, 76}, "Files", ui::Icon::Folder, ui::color::IconOrange,
        true);
    add({212, 127, 92, 76}, "Titles", ui::Icon::GameCard, ui::color::IconGold,
        true);
    spec.drawTop = [&]() {
        // The answer to the update check can arrive while this is open.
        spec.controls[0].badge = updateAvailable_ ? "New" : "";
        refreshStatus(false);
        ui::drawTopBackground();
        ui::drawStatusBar();
        ui::drawDots(41);
        const ui::Rect panel = {17, 53, 366, 134};
        ui::drawPanel(panel);
        ui::drawIcon(ui::Icon::AppMark, 171, 60, 58, ui::color::Accent);
        ui::drawText("3dsDL", 200, 116, ui::type::Hero, ui::color::Ink,
                     C2D_AlignCenter);
        const std::string where =
            "Saves to " + settings_.destination +
            (freeBytes_ >= 0 ? "   " +
                                   units::bytes(static_cast<uint64_t>(
                                       freeBytes_)) +
                                   " free"
                             : "");
        ui::drawMarquee(where, 200, 156, ui::type::Small, ui::color::InkSoft,
                        340, C2D_AlignCenter);
        ui::drawDots(196);
        ui::drawText("Ver. " APP_VERSION, 387, 210, 0.66f,
                     ui::color::rgb(0x63593F), C2D_AlignRight);
    };

    const ui::ScreenResult result = ui::runScreen(spec, homeSelection_);
    switch (result.choice) {
    case 0:
        return Action::Settings;
    case 1:
        return Action::Queue;
    case 2:
        return Action::Scan;
    case 3:
        return Action::EnterUrl;
    case 4:
        return Action::Receive;
    case 5:
        return Action::History;
    case 6:
        return Action::Files;
    case 7:
        return Action::Titles;
    default:
        break;
    }
    return Action::Exit;
}

// --- Downloading -------------------------------------------------------------

App::Start App::startDownload(const std::string &address, std::string folder)
{
    const std::string link = text::trim(address);
    if (link.empty()) {
        return Start::Cancelled;
    }
    if (!url::isHttpUrl(link)) {
        ui::message("Not a download link",
                    "3dsDL can only download direct http:// or https:// "
                    "links.",
                    ui::Tone::Error, link);
        return Start::Cancelled;
    }
    const Confirm confirmed = confirmDownload(link, folder);
    if (confirmed == Confirm::Queue) {
        addToQueue({link}, folder, true);
        return Start::Queued;
    }
    if (confirmed != Confirm::Download) {
        return Start::Cancelled;
    }
    if (!fs::makeDirectories(folder)) {
        ui::message("Folder unavailable", "Could not create " + folder + ".",
                    ui::Tone::Error);
        return Start::Done;
    }

    if (platform::wifiStatus() == platform::Wifi::Disconnected) {
        ui::Dialog dialog;
        dialog.header = "Download";
        dialog.title = "The system is not connected to the internet.";
        dialog.tone = ui::Tone::Warning;
        dialog.body.push_back({"Turn wireless communication on, or check the "
                               "connection in System Settings.",
                               ui::color::Text, 0.5f, 3});
        dialog.buttons = {"Try Anyway", "Cancel"};
        dialog.initial = 1;
        dialog.cancelButton = 1;
        if (ui::choose(dialog) != 0) {
            return Start::Cancelled;
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
                entry.name = result.filename;
                entry.resumable = true;
                entry.bytes = result.bytes;
                record(entry);
            }
            return Start::Done;
        }

        if (result.outcome == net::Outcome::Completed) {
            finishDownload(link, folder, result);
            return Start::Done;
        }
        if (result.outcome == net::Outcome::Cancelled) {
            HistoryEntry entry =
                newEntry(link, folder, HistoryStatus::Cancelled);
            entry.message = result.message;
            entry.name = result.filename;
            record(entry);
            return Start::Done;
        }

        HistoryEntry entry = newEntry(link, folder, HistoryStatus::Failed);
        entry.message = result.message;
        entry.name = result.filename;
        entry.resumable = result.resumable;
        entry.bytes = result.bytes;
        record(entry);

        ui::Dialog dialog;
        dialog.header = "Download";
        dialog.title = "The download could not be completed.";
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
        if (!result.detail.empty() && result.detail != result.message &&
            !result.noSpace) {
            dialog.body.push_back({result.detail, ui::color::Muted, 0.4f, 2});
        }
        // A CIA too big for the SD card can still go straight into the
        // install service, as FBI did.
        const std::string fileName = result.filename.empty()
                                         ? naming::chooseFilename("", link)
                                         : result.filename;
        if (result.noSpace && installer::available() &&
            text::endsWithNoCase(fileName, ".cia")) {
            dialog.body.push_back({"Install Directly puts it straight into the "
                                   "install service without saving the file. "
                                   "It starts over if the connection drops.",
                                   ui::color::Muted, 0.44f, 3});
            dialog.buttons = {"Install Directly", "Close"};
            dialog.cancelButton = 1;
            if (ui::choose(dialog) == 0) {
                installDirectly(link);
            }
            return Start::Done;
        }
        if (result.certificateProblem) {
            dialog.buttons = {"Check Clock", "Retry", "Close"};
            dialog.cancelButton = 2;
        } else {
            dialog.buttons = {"Retry", "Close"};
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
            return Start::Done;
        }
        // The retry gets its own history entry.
        history_.erase(history_.begin());
    }
    return Start::Done;
}

App::Confirm App::confirmDownload(const std::string &address,
                                  std::string &folder)
{
    int focus = ui::ScreenAction;
    while (ui::keepRunning()) {
        const uint64_t partial = settings_.resumeDownloads
            ? net::partialSize(folder, address)
            : 0;
        const std::string name = naming::chooseFilename("", address);
        const bool secure = url::isHttps(address);

        ui::ScreenSpec spec;
        spec.back = "Cancel";
        spec.action = "Download";
        ui::Control change;
        change.rect = {46, 124, 228, 34};
        change.label = "Change Folder";
        change.icon = ui::Icon::Folder;
        change.iconColor = ui::color::IconOrange;
        change.scale = ui::type::Body + 0.06f;
        spec.controls.push_back(change);
        ui::Control queue = change;
        queue.rect = {46, 166, 228, 34};
        queue.label = "Add to Queue";
        queue.icon = ui::Icon::Download;
        queue.iconColor = ui::color::IconGreen;
        spec.controls.push_back(queue);
        spec.drawTop = [&]() {
            ui::drawTopChrome("Download", ui::Icon::Download,
                              ui::color::IconGreen);
            ui::drawPanel();
            const float left = ui::TopPanel.x + 18;
            ui::drawIcon(name == naming::DefaultFilename ? ui::Icon::File
                                                         : ui::Icon::Package,
                         left, ui::TopPanel.y + 14, 30, ui::color::Accent);
            ui::drawMarquee(name == naming::DefaultFilename ? url::host(address)
                                                            : name,
                            left + 40, ui::TopPanel.y + 10, 0.62f,
                            ui::color::InkStrong, 300);
            ui::drawText("from " + url::host(address), left + 40,
                         ui::TopPanel.y + 36, ui::type::Small,
                         ui::color::InkSoft);
            float y = ui::drawWrapped(address, left, ui::TopPanel.y + 58,
                                      ui::type::Tiny, ui::color::InkSoft,
                                      ui::TopPanel.w - 36, 2) + 4;
            y = ui::drawTagRow(left, y, 74, "Save to", folder);
            if (partial > 0) {
                ui::drawTagRow(left, y, 74, "Resume",
                               units::bytes(partial) + " is already here",
                               ui::color::Good);
            } else if (secure) {
                ui::drawTagRow(left, y, 74, "HTTPS",
                               "The certificate is checked.",
                               ui::color::Good);
            } else {
                ui::drawTagRow(left, y, 74, "HTTP",
                               "Not encrypted on the way.",
                               ui::color::rgb(0xD2603A));
            }
        };
        spec.drawBottom = [&]() {
            ui::drawText("Download this file?", 160, 14, ui::type::Body,
                         ui::color::Ink, C2D_AlignCenter);
            const ui::Rect box = {13, 46, 294, 70};
            ui::drawRecess(box);
            ui::drawText("Saves to", box.x + 12, box.y + 8, ui::type::Small,
                         ui::color::InkSoft);
            ui::drawMarquee(folder, box.x + 12, box.y + 32, ui::type::Body,
                            ui::color::InkStrong, 270);
        };

        const ui::ScreenResult result = ui::runScreen(spec, focus);
        if (result.choice == ui::ScreenAction) {
            return Confirm::Download;
        }
        if (result.choice == 1) {
            return Confirm::Queue;
        }
        if (result.choice != 0) {
            return Confirm::Cancel;
        }
        std::string chosen = folder;
        if (chooseFolder(chosen)) {
            folder = chosen;
            settings_.destination = chosen;
            persistSettings();
        }
    }
    return Confirm::Cancel;
}

void App::finishDownload(const std::string &address, const std::string &folder,
                         const net::Result &result)
{
    bool overwrite = false;
    if (fs::fileExists(folder + result.filename)) {
        const std::string suggestion = basename(
            naming::unusedPath(folder, result.filename, fs::fileExists));

        ui::Dialog dialog;
        dialog.header = "Download";
        dialog.title = "A file with this name already exists. What would "
                       "you like to do?";
        dialog.tone = ui::Tone::Warning;
        dialog.body.push_back({result.filename + " is already in " + folder +
                                   ".",
                               ui::color::Text, 0.56f, 2});
        dialog.body.push_back({"Keep Both saves the new file as " + suggestion +
                                   ".",
                               ui::color::Muted, 0.46f, 3});
        dialog.body.push_back({"Replace swaps the old file for the new one.",
                               ui::color::Muted, 0.46f, 2});
        dialog.buttons = {"Keep Both", "Replace", "Cancel"};
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
    entry.name = result.filename;
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
                               "). Here extracts into " + folder +
                               ", New Folder into " + ownFolder + ".",
                           ui::color::Text, 0.46f, 5});
    dialog.buttons = {"Here", "New Folder", "Not Now"};
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
        question.title = plural(clashes.size(), "file") + " already exist. Replace them?";
        question.tone = ui::Tone::Warning;
        question.body.push_back({"For example " + clashes.front() + " in " +
                                     target,
                                 ui::color::Text, 0.46f, 3});
        question.buttons = {"Overwrite", "Skip Them", "Cancel"};
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
    done.title = "The archive has been extracted.";
    done.tone = ui::Tone::Success;
    done.body.push_back({extracted, ui::color::Text, 0.5f, 3});
    done.body.push_back({notes, ui::color::Muted, 0.44f, 4});
    done.buttons = {"Done", "Delete ZIP"};
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
        ui::ScreenSpec spec;
        spec.back = "Keep File";
        ui::Control install;
        install.rect = {26, 46, 268, 38};
        install.label = "Install";
        spec.controls.push_back(install);
        ui::Control both = install;
        both.rect = {26, 97, 268, 38};
        both.label = "Install and Delete File";
        both.scale = ui::type::Body + 0.06f;
        spec.controls.push_back(both);
        spec.drawTop = [&]() {
            screens::drawCiaTop(info, "Download Complete", ui::Icon::Download,
                                ui::color::IconGreen);
        };
        spec.drawBottom = [&]() {
            ui::drawText("Install this software?", 160, 13, ui::type::Body,
                         ui::color::Ink, C2D_AlignCenter);
            ui::drawWrapped(screens::installState(info) +
                                " To always do the same, see Settings, "
                                "Downloads.",
                            160, 148, ui::type::Tiny, ui::color::InkSoft, 270,
                            3, C2D_AlignCenter);
        };
        int focus = 0;
        const ui::ScreenResult result = ui::runScreen(spec, focus);
        if (result.choice != 0 && result.choice != 1) {
            ui::message("File Kept", summary, ui::Tone::Success,
                        "Install it later from Files or History.");
            return;
        }
        action = result.choice == 0 ? CiaAction::Install
                                    : CiaAction::InstallAndDelete;
    }
    installCia(path, info, action == CiaAction::InstallAndDelete, address);
}

bool App::installCia(const std::string &path, const cia::Info &info,
                     bool deleteAfter, const std::string &address, bool quiet,
                     bool *alreadyInstalled)
{
    if (alreadyInstalled != nullptr) {
        *alreadyInstalled = false;
    }
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
        dialog.title = name + " is made for New Nintendo 3DS systems.";
        dialog.tone = ui::Tone::Warning;
        dialog.icon = ui::showIcon(info.smdh.icon);
        dialog.body.push_back({"It will install, but will not start on this "
                               "console.",
                               ui::color::Text, 0.5f, 2});
        dialog.buttons = {"Install Anyway", "Cancel"};
        dialog.initial = 1;
        dialog.cancelButton = 1;
        if (ui::choose(dialog) != 0) {
            return false;
        }
    }

    // AM refuses to go back to an older version, or to install the same
    // version again, so both take removing the installed one first, which
    // can take its save data with it.
    uint16_t installed = 0;
    const bool isInstalled = installer::installedVersion(info.titleId,
                                                         installed);
    // sameVersion asks gently and starts on Reinstall: installing that
    // version again was asked for. Going back a version is the risky one.
    const auto confirmReplace = [&](const std::string &title,
                                    bool sameVersion = false) {
        ui::Dialog dialog;
        dialog.header = "install";
        dialog.title = title;
        dialog.tone = sameVersion ? ui::Tone::Info : ui::Tone::Warning;
        dialog.icon = ui::showIcon(info.smdh.icon);
        dialog.body.push_back({"Installed: " + cia::versionText(installed) +
                                   "\nThis file: " +
                                   cia::versionText(info.version),
                               ui::color::Text, 0.48f, 2});
        dialog.body.push_back({"3dsDL backs up its save, removes the "
                               "installed copy and installs this one.",
                               ui::color::Muted, 0.46f, 3});
        if (sameVersion) {
            dialog.buttons = {"Reinstall", "Keep Installed"};
            dialog.initial = 0;
        } else {
            dialog.buttons = {"Replace", "Cancel"};
            dialog.dangerous = 0;
            dialog.initial = 1;
        }
        dialog.cancelButton = 1;
        return ui::choose(dialog) == 0;
    };
    bool removeFirst = false;
    if (isInstalled && installed == info.version) {
        // AM answers 0xC8E083FC to the same version, after the whole file.
        u64 running = 0;
        const bool isRunning = R_SUCCEEDED(APT_GetProgramID(&running)) &&
                               running == info.titleId;
        bool reinstall = false;
        if (isRunning) {
            ui::message("Already Installed",
                        name + " " + cia::versionText(installed) +
                            " is already installed. It is the software "
                            "running now, so it cannot be installed again.",
                        ui::Tone::Info);
        } else {
            reinstall = confirmReplace("This version of " + name +
                                           " is already installed",
                                       true);
        }
        if (!reinstall) {
            // The file stays even after Install and Delete: nothing went in.
            if (!address.empty()) {
                markInstalled(address, path, "Already installed");
            }
            if (alreadyInstalled != nullptr) {
                *alreadyInstalled = true;
            }
            return true;
        }
        removeFirst = true;
    } else if (isInstalled && installed > info.version) {
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
    std::string saveNote;
    bool saveKept = false;
    while (ui::keepRunning()) {
        if (removeFirst && !saveKept) {
            if (!saveBeforeRemoving(info, name, installed, saveNote)) {
                return false;
            }
            saveKept = true;
        }
        if (removeFirst) {
            std::string error;
            if (!installer::removeTitle(info.titleId, error)) {
                ui::message("Could not replace " + name, error,
                            ui::Tone::Error);
                return false;
            }
        }
        result = tasks::install(path, info.titleId, name,
                                settings_.keepAwake, info.smdh.icon);
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

    if (!saveNote.empty()) {
        notes.push_back({saveNote, ui::color::Muted, 0.42f, 3});
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
    dialog.header = "Installation Complete";
    const bool onHome = kind == cia::Kind::Application ||
                        kind == cia::Kind::Demo;
    dialog.title = name + " has been installed." +
                   (onHome ? " You can find it on the HOME Menu." : "");
    dialog.tone = ui::Tone::Success;
    dialog.icon = ui::showIcon(info.smdh.icon);
    dialog.body.push_back({name, ui::color::Text, 0.5f, 1});
    dialog.body.push_back(
        {std::string(cia::kindName(cia::kind(info.titleId))) + ", " +
             cia::versionText(info.version) + ", " +
             units::bytes(result.bytes) +
             (result.seconds > 0.5
                  ? ", installed in " + units::duration(static_cast<int64_t>(
                                            result.seconds + 0.5))
                  : ""),
         ui::color::Muted, 0.46f, 2});
    dialog.body.insert(dialog.body.end(), notes.begin(), notes.end());
    if (canLaunch) {
        dialog.buttons = {"Close", "Launch"};
        dialog.initial = 1;
        dialog.cancelButton = 0;
    } else {
        dialog.buttons = {"Close"};
    }
    if (ui::choose(dialog) == 1 && canLaunch) {
        std::string error;
        if (!installer::launch(launchId, error)) {
            ui::message("Could Not Launch", error, ui::Tone::Error);
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
        spec.title = "Download Folder";
        ui::ListItem browse;
        browse.label = "Browse the SD Card...";
        browse.labelColor = ui::color::Accent;
        browse.icon = ui::Icon::Folder;
        browse.iconColor = ui::color::IconOrange;
        spec.items.push_back(browse);
        ui::ListItem type;
        type.label = "Type a Folder...";
        type.labelColor = ui::color::Accent;
        type.icon = ui::Icon::Text;
        spec.items.push_back(type);

        const auto addFolder = [&](const std::string &label,
                                   const std::string &path) {
            ui::ListItem item;
            item.label = label;
            item.detail = path;
            if (!fs::directoryExists(path)) {
                item.badge = "New";
                item.badgeColor = ui::color::Tag;
            }
            spec.items.push_back(item);
            paths.push_back(path);
        };
        for (const Preset &preset : Presets) {
            addFolder(preset.label, preset.path);
        }
        for (const HistoryEntry &entry : history_) {
            if (paths.size() >= 2 + std::size(Presets) + MaxRecentFolders) {
                break;
            }
            if (std::find(paths.begin(), paths.end(), entry.folder) ==
                    paths.end() &&
                fs::directoryExists(entry.folder)) {
                addFolder("Recent", entry.folder);
            }
        }

        spec.drawTop = [&](int index) {
            ui::drawTopChrome("Download Folder", ui::Icon::Folder,
                              ui::color::IconOrange);
            ui::drawPanel();
            const float left = ui::TopPanel.x + 18;
            float y = ui::TopPanel.y + 20;
            y = ui::drawTagRow(left, y, 82, "Now", folder);
            std::string note;
            if (index == 0) {
                note = "Pick any folder on the SD card, or make a new one.";
            } else if (index == 1) {
                note = "Type a path that starts with sdmc:/.";
            } else if (index >= 2) {
                y = ui::drawTagRow(left, y, 82, "Selected", paths[index],
                                   ui::color::Good);
                note = fs::directoryExists(paths[index])
                           ? "New downloads will be saved here."
                           : "It will be created when you choose it.";
            }
            ui::drawWrapped(note, 200, y + 10, ui::type::Small,
                            ui::color::InkSoft, 320, 2, C2D_AlignCenter);
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
            ui::message("Folder Unavailable", "Could not create " + path + ".",
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

        ui::ListSpec spec;
        spec.title = current;
        spec.back = atRoot ? "Cancel" : "Back";
        spec.action = "Use This";
        spec.actionKey = "Y";
        spec.actionKeys = KEY_Y;
        spec.extraKeys = KEY_X;
        ui::ListItem make;
        make.label = "New Folder...";
        make.labelColor = ui::color::Accent;
        make.icon = ui::Icon::Folder;
        make.iconColor = ui::color::IconGreen;
        spec.items.push_back(make);
        for (const std::string &name : names) {
            ui::ListItem item;
            item.label = name;
            item.icon = ui::Icon::Folder;
            item.iconColor = ui::color::IconOrange;
            spec.items.push_back(item);
        }
        spec.drawTop = [&](int index) {
            ui::drawTopChrome("Choose a Folder", ui::Icon::Folder,
                              ui::color::IconOrange);
            ui::drawPanel();
            const float left = ui::TopPanel.x + 18;
            float y = ui::TopPanel.y + 20;
            y = ui::drawTagRow(left, y, 82, "Browsing", current);
            if (index >= 1) {
                y = ui::drawTagRow(left, y, 82, "Open",
                                   current + names[index - 1] + "/");
            }
            ui::drawWrapped("Open folders with A or a tap. Use This saves "
                            "downloads in the folder being browsed.",
                            200, y + 10, ui::type::Small, ui::color::InkSoft,
                            320, 3, C2D_AlignCenter);
        };

        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key == 0) {
            return false;
        }
        if (result.key == KEY_Y) {
            folder = current;
            return true;
        }
        if (result.key == KEY_B) {
            if (atRoot) {
                return false;
            }
            current = naming::parentFolder(current);
            cursor = 0;
            continue;
        }
        if (result.key == KEY_X || (result.key == KEY_A && result.index == 0)) {
            std::string name;
            if (platform::keyboard(platform::KeyboardKind::Name,
                                   "New folder name", "", name)) {
                const std::string created =
                    current + naming::safeFilename(text::trim(name)) + "/";
                if (fs::makeDirectories(created)) {
                    current = created;
                    cursor = 0;
                } else {
                    ui::message("Could Not Create the Folder", created,
                                ui::Tone::Error);
                }
            }
            continue;
        }
        if (result.index >= 1) {
            current += names[result.index - 1] + "/";
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
        dialog.buttons = {"Create", "Cancel"};
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
    dialog.buttons = {"Clear", "Cancel"};
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
    const auto badgeFor = [](const HistoryEntry &entry, std::string &label,
                             u32 &fill) {
        const bool canResume =
            entry.resumable && net::partialSize(entry.folder, entry.url) > 0;
        if (canResume) {
            label = "Resume";
            fill = ui::color::Tag;
            return;
        }
        switch (entry.status) {
        case HistoryStatus::Installed:
            label = "Installed";
            fill = ui::color::Good;
            return;
        case HistoryStatus::Saved:
            label = "Saved";
            fill = ui::color::Neutral;
            return;
        case HistoryStatus::Failed:
            label = "Failed";
            fill = ui::color::Bad;
            return;
        case HistoryStatus::Cancelled:
            break;
        }
        label = "Stopped";
        fill = ui::color::Neutral;
    };
    // The highlighted download in full, on the top screen.
    const auto drawEntry = [&](const HistoryEntry &entry) {
        ui::drawPanel();
        const float left = ui::TopPanel.x + 18;
        std::string badge;
        u32 fill = 0;
        badgeFor(entry, badge, fill);
        const float width = std::max(34.0f, ui::textWidth(badge, 0.34f) + 10);
        ui::drawBadge(left + width, ui::TopPanel.y + 14, badge, fill);
        ui::drawText(units::dateTime(entry.time), left + width + 10,
                     ui::TopPanel.y + 13, ui::type::Small, ui::color::InkSoft);
        ui::drawMarquee(entryName(entry), left, ui::TopPanel.y + 36, 0.62f,
                        ui::color::InkStrong, 330);
        float y = ui::drawWrapped(entry.url, left, ui::TopPanel.y + 62,
                                  ui::type::Tiny, ui::color::InkSoft, 330, 2) +
                  4;
        const std::string where =
            entry.status == HistoryStatus::Saved ? "Saved as " + entry.path
            : entry.status == HistoryStatus::Installed
                ? entry.message + ": " + entry.path
                : entry.message;
        y = ui::drawWrapped(where, left, y, ui::type::Small, ui::color::Ink,
                            330, 2) + 4;
        const uint64_t partial =
            entry.resumable ? net::partialSize(entry.folder, entry.url) : 0;
        if (partial > 0 && y < ui::TopPanel.y + ui::TopPanel.h - 24) {
            ui::drawTagRow(left, y, 86, "Can resume",
                           units::bytes(partial) + " is kept in " + entry.folder,
                           ui::color::Good);
        }
    };

    while (ui::keepRunning()) {
        ui::ListSpec spec;
        spec.title = "Recent Downloads";
        spec.emptyText = "Nothing here yet. Finished, failed and stopped "
                         "downloads are listed here.";
        if (!history_.empty()) {
            spec.action = "Clear";
            spec.actionKey = "X";
            spec.actionKeys = KEY_X;
        }
        spec.hint = history_.empty() ? "" : std::to_string(history_.size()) +
                                                " saved";
        for (const HistoryEntry &entry : history_) {
            ui::ListItem item;
            item.label = entryName(entry);
            badgeFor(entry, item.badge, item.badgeColor);
            spec.items.push_back(item);
        }
        spec.drawTop = [&](int index) {
            ui::drawTopChrome("History", ui::Icon::History, ui::color::IconRed);
            if (index < 0) {
                ui::drawPanel();
                ui::drawPanelText("Your downloads will appear here.",
                                  ui::TopPanel);
                return;
            }
            drawEntry(history_[index]);
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
        // A CIA that is still on the SD card can be installed from here.
        cia::Info info;
        std::string error;
        const bool installable = !entry.path.empty() &&
                                 cia::isCiaFile(entry.path) &&
                                 cia::readInfo(entry.path, info, error);
        enum class Action { Install, Download, Remove };
        std::vector<ui::MenuItem> items;
        std::vector<Action> actions;
        if (installable) {
            items.push_back({"Install"});
            actions.push_back(Action::Install);
        }
        items.push_back({canResume ? "Resume Download" : "Download Again"});
        actions.push_back(Action::Download);
        items.push_back({"Remove from History", true});
        actions.push_back(Action::Remove);
        const int choice = ui::menu(items, [&](int) {
            ui::drawTopChrome("History", ui::Icon::History, ui::color::IconRed);
            drawEntry(entry);
        });
        if (choice < 0) {
            continue;
        }
        switch (actions[choice]) {
        case Action::Install:
            installCia(entry.path, info, false, entry.url);
            break;
        case Action::Download:
            startDownload(entry.url, fs::directoryExists(entry.folder)
                                         ? entry.folder
                                         : settings_.destination);
            cursor = 0;
            break;
        case Action::Remove:
            if (entry.resumable) {
                net::discardPartial(entry.folder, entry.url);
            }
            history_.erase(history_.begin() + result.index);
            storage::saveHistory(history_);
            break;
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
    // A list of settings with their values; A opens a choice for one.
    struct Row {
        std::string label;
        std::string value;
        std::string help;
    };
    const auto settingsList = [&](const std::string &title, ui::Icon icon,
                                  u32 iconColor,
                                  const std::function<std::vector<Row>()> &rows,
                                  const std::function<void(int)> &change) {
        int cursor = 0;
        while (ui::keepRunning()) {
            const std::vector<Row> current = rows();
            ui::ListSpec spec;
            spec.title = title;
            for (const Row &row : current) {
                ui::ListItem item;
                item.label = row.label;
                item.badge = row.value;
                item.badgeColor = ui::color::Tag;
                spec.items.push_back(item);
            }
            spec.drawTop = [&](int index) {
                ui::drawTopChrome(title, icon, iconColor);
                ui::drawPanel();
                if (index >= 0) {
                    ui::drawPanelText(current[index].help, ui::TopPanel);
                }
            };
            const ui::ListResult result = ui::runList(spec, cursor);
            if (result.key != KEY_A) {
                return;
            }
            change(result.index);
        }
    };
    const auto onOffChoice = [&](const std::string &title,
                                 const std::string &help, bool &value) {
        const int picked = ui::chooseOption(title, help, {"On", "Off"},
                                            value ? 0 : 1, ui::Icon::Download,
                                            ui::color::IconGreen);
        if (picked >= 0 && (picked == 0) != value) {
            value = picked == 0;
            persistSettings();
        }
    };

    static const char *const ResumeHelp =
        "When a download is interrupted, keep what arrived and continue from "
        "there next time. 3dsDL only resumes when the server confirms the "
        "file has not changed.";
    static const char *const AwakeHelp =
        "Pause Sleep Mode while a download or install runs. Closing the "
        "system would otherwise turn Wi-Fi off and stop the transfer.";
    static const char *const ZipHelp =
        "After a ZIP file finishes downloading, ask whether to extract it. "
        "Nothing is ever extracted without asking.";
    static const char *const CiaHelp =
        "Choose what 3dsDL does when a CIA finishes downloading. Ask Each "
        "Time shows the icon and version before anything is installed.";

    const auto downloads = [&]() {
        settingsList(
            "Downloads", ui::Icon::Download, ui::color::IconGreen,
            [&]() {
                return std::vector<Row>{
                    {"Resume Interrupted Downloads",
                     onOff(settings_.resumeDownloads), ResumeHelp},
                    {"Stay Awake While Downloading", onOff(settings_.keepAwake),
                     AwakeHelp},
                    {"Offer to Extract ZIP Files",
                     onOff(settings_.offerZipExtraction), ZipHelp},
                    {"After a CIA Downloads",
                     ciaActionLabel(settings_.afterCiaDownload), CiaHelp}};
            },
            [&](int index) {
                if (index == 0) {
                    onOffChoice("Resume Downloads", ResumeHelp,
                                settings_.resumeDownloads);
                } else if (index == 1) {
                    onOffChoice("Stay Awake", AwakeHelp, settings_.keepAwake);
                } else if (index == 2) {
                    onOffChoice("Extract ZIP Files", ZipHelp,
                                settings_.offerZipExtraction);
                } else if (index == 3) {
                    const int picked = ui::chooseOption(
                        "After a CIA Downloads", CiaHelp,
                        {"Ask Each Time", "Install", "Install and Delete File",
                         "Only Keep the File"},
                        static_cast<int>(settings_.afterCiaDownload),
                        ui::Icon::Package, ui::color::IconBlue);
                    if (picked >= 0) {
                        settings_.afterCiaDownload =
                            static_cast<CiaAction>(picked);
                        persistSettings();
                    }
                }
            });
    };
    const auto filesAndHistory = [&]() {
        settingsList(
            "Other Settings", ui::Icon::Wrench, ui::color::IconGrey,
            [&]() {
                static const char *const Sorts[] = {"Name", "Size", "Date",
                                                    "Type"};
                return std::vector<Row>{
                    {"Sort Files", Sorts[static_cast<int>(settings_.fileSort)],
                     "How Files orders what is in a folder. Folders always "
                     "come first."},
                    {"Show Hidden Files", onOff(settings_.showHiddenFiles),
                     "Show files and folders whose names start with a dot."},
                    {"Sounds",
                     sound::available() ? onOff(settings_.sounds)
                                        : "Unavailable",
                     sound::available()
                         ? "Soft sounds when moving, choosing and going back, "
                           "as in the system menus."
                         : "Sounds need the DSP firmware dump, "
                           "sdmc:/3ds/dspfirm.cdc. Make it with DSP1, then "
                           "start 3dsDL again."},
                    {"Clear Download History",
                     std::to_string(history_.size()),
                     "Removes the list of past downloads and any partial "
                     "downloads kept for resuming. Downloaded files stay."}};
            },
            [&](int index) {
                if (index == 0) {
                    const int picked = ui::chooseOption(
                        "Sort Files",
                        "Folders always come first, sorted by name.",
                        {"Name", "Size, Largest First", "Date, Newest First",
                         "Type"},
                        static_cast<int>(settings_.fileSort), ui::Icon::Folder,
                        ui::color::IconOrange);
                    if (picked >= 0) {
                        settings_.fileSort = static_cast<fs::SortOrder>(picked);
                        persistSettings();
                    }
                } else if (index == 1) {
                    onOffChoice("Show Hidden Files",
                                "Show files and folders whose names start "
                                "with a dot.",
                                settings_.showHiddenFiles);
                } else if (index == 2) {
                    if (!sound::available()) {
                        ui::message("Sounds Unavailable",
                                    "Sounds need the DSP firmware dump, "
                                    "sdmc:/3ds/dspfirm.cdc. Make it with the "
                                    "DSP1 homebrew, then start 3dsDL again.");
                    } else {
                        onOffChoice("Sounds",
                                    "Soft sounds when moving, choosing and "
                                    "going back.",
                                    settings_.sounds);
                        sound::setEnabled(settings_.sounds);
                    }
                } else if (history_.empty()) {
                    ui::message("History Is Empty", "There is nothing to clear.");
                } else {
                    confirmClearHistory();
                }
            });
    };

    static const char *const TileHelp[] = {
        "Version, credits and where 3dsDL keeps its files.",
        "Check for a newer 3dsDL and install it.",
        "Resuming, Sleep Mode, ZIP files and what happens after a CIA "
        "downloads.",
        "The folder new downloads are saved to.",
        "Compare the console's clock with the time online and set it. Secure "
        "downloads fail when the date is wrong.",
        "How Files sorts and shows files, sounds, and the download history.",
    };
    int focus = 2;
    while (ui::keepRunning() && !quit_) {
        ui::ScreenSpec spec;
        spec.back = "Back";
        spec.action = "Report Issue";
        const auto add = [&](ui::Rect rect, const char *label, ui::Icon icon,
                             u32 iconColor, bool tile) {
            ui::Control control;
            control.rect = rect;
            control.label = label;
            control.icon = icon;
            control.iconColor = iconColor;
            control.tile = tile;
            control.scale = ui::type::Small + 0.06f;
            spec.controls.push_back(control);
        };
        add({16, 6, 140, 26}, "About", ui::Icon::AppMark, ui::color::Accent,
            false);
        add({164, 6, 140, 26}, "Updates", ui::Icon::Download,
            ui::color::IconGreen, false);
        add({16, 42, 140, 78}, "Downloads", ui::Icon::Download,
            ui::color::IconGreen, true);
        add({164, 42, 140, 78}, "Download Folder", ui::Icon::Folder,
            ui::color::IconOrange, true);
        add({16, 127, 140, 76}, "Console Clock", ui::Icon::Clock,
            ui::color::IconBlue, true);
        add({164, 127, 140, 76}, "Other Settings", ui::Icon::Wrench,
            ui::color::IconGrey, true);
        spec.drawTop = [&]() {
            ui::drawTopChrome("Settings", ui::Icon::Wrench, ui::color::IconGrey);
            ui::drawPanel();
            spec.controls[1].badge = updateAvailable_ ? "New" : "";
            const float left = ui::TopPanel.x + 18;
            if (focus == ui::ScreenAction) {
                ui::drawWrapped("Where to report a bug, ask a question or "
                                "reach the developer, as QR codes to scan "
                                "with a phone.",
                                left, ui::TopPanel.y + 12, ui::type::Small,
                                ui::color::Ink, ui::TopPanel.w - 36, 3);
                return;
            }
            const int shown = focus >= 0 && focus < 6 ? focus : 2;
            float y = ui::drawWrapped(TileHelp[shown], left, ui::TopPanel.y + 12,
                                      ui::type::Small, ui::color::Ink,
                                      ui::TopPanel.w - 36, 3) +
                      10;
            if (shown == 1) {
                const updater::Status update = updater::status();
                y = ui::drawTagRow(left, y, 96, "Installed", APP_VERSION);
                if (update.state == updater::State::Available) {
                    ui::drawTagRow(left, y, 96, "New", update.release.version,
                                   ui::color::Good);
                } else {
                    ui::drawTagRow(left, y, 96, "At Startup",
                                   settings_.checkForUpdates ? "Check"
                                                             : "Don't Check");
                }
            } else if (shown == 3) {
                ui::drawTagRow(left, y, 96, "Saves to", settings_.destination);
            } else if (shown == 4) {
                ui::drawTagRow(left, y, 96, "Console",
                               units::dateTime(platform::now()));
            } else if (shown == 5) {
                y = ui::drawTagRow(left, y, 96, "History",
                                   plural(history_.size(), "download"));
                ui::drawTagRow(left, y, 96, "Sounds",
                               sound::available() ? onOff(settings_.sounds)
                                                  : "Unavailable");
            } else if (shown == 2) {
                y = ui::drawTagRow(left, y, 96, "Resume",
                                   onOff(settings_.resumeDownloads));
                ui::drawTagRow(left, y, 96, "After a CIA",
                               ciaActionLabel(settings_.afterCiaDownload));
            } else {
                ui::drawTagRow(left, y, 96, "Version", APP_VERSION);
            }
        };
        const ui::ScreenResult result = ui::runScreen(spec, focus);
        switch (result.choice) {
        case 0:
            aboutScreen();
            break;
        case 1:
            softwareUpdateScreen();
            break;
        case 2:
            downloads();
            break;
        case 3:
            changeDefaultFolder();
            break;
        case 4:
            clockScreen();
            break;
        case 5:
            filesAndHistory();
            break;
        case ui::ScreenAction:
            reportScreen();
            break;
        default:
            return;
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
    spec.title = "Time Zone";
    for (const tz::Zone &zone : zones) {
        ui::ListItem item;
        item.label = zone.places;
        item.badge = tz::formatOffset(zone.offsetMinutes);
        item.badgeColor = ui::color::Tag;
        if (settings_.timeZoneSet &&
            zone.offsetMinutes == settings_.timeZoneMinutes) {
            item.icon = ui::Icon::Check;
            item.iconColor = ui::color::IconGreen;
        }
        spec.items.push_back(item);
    }
    spec.drawTop = [&](int index) {
        ui::drawTopChrome("Time Zone", ui::Icon::Clock, ui::color::IconBlue);
        ui::drawPanel();
        if (index < 0) {
            return;
        }
        const tz::Zone &zone = zones[index];
        const float left = ui::TopPanel.x + 18;
        float y = ui::TopPanel.y + 20;
        y = ui::drawTagRow(left, y, 92, "Offset",
                           tz::formatOffset(zone.offsetMinutes));
        if (utcNowSeconds >= 0) {
            y = ui::drawTagRow(
                left, y, 92, "Time there",
                units::dateTime(utcNowSeconds + zone.offsetMinutes * 60 +
                                static_cast<int64_t>(
                                    (osGetTime() - openedAt) / 1000)));
        }
        ui::drawWrapped("Pick the offset in use right now. Summer time "
                        "usually adds one hour.",
                        200, y + 12, ui::type::Small, ui::color::InkSoft, 320,
                        2, C2D_AlignCenter);
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
        tasks::busy("Console Clock", "Asking a time server for the current "
                                     "time...", [&]() {
            network = ntp::queryAny(ntp::defaultServers());
        });
        const u64 answeredAt = osGetTime();
        if (!ui::keepRunning()) {
            return false;
        }

        if (!network.ok) {
            ui::Dialog dialog;
            dialog.header = "Console Clock";
            dialog.title = "The time could not be checked online.";
            dialog.tone = ui::Tone::Error;
            dialog.body.push_back({network.error, ui::color::Text, 0.5f, 3});
            dialog.body.push_back({"Check the Wi-Fi connection. Some networks "
                                   "also block time servers (UDP port 123).",
                                   ui::color::Muted, 0.44f, 3});
            dialog.buttons = {"Try Again", "Back"};
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

            ui::ScreenSpec spec;
            spec.back = "Back";
            ui::Control set;
            set.rect = {26, 26, 268, 40};
            set.label = "Set the Clock";
            spec.controls.push_back(set);
            ui::Control change = set;
            change.rect = {26, 80, 268, 40};
            change.label = "Change Time Zone";
            spec.controls.push_back(change);
            spec.drawTop = [&]() {
                ui::drawTopChrome("Console Clock", ui::Icon::Clock,
                                  ui::color::IconBlue);
                ui::drawPanel();
                const float left = ui::TopPanel.x + 18;
                float y = ui::TopPanel.y + 14;
                y = ui::drawTagRow(left, y, 108, "This console",
                                   units::dateTime(platform::now()));
                y = ui::drawTagRow(left, y, 108, "Internet time",
                                   units::dateTime((utcNowMs() + offset * 60000LL) /
                                                   1000),
                                   ui::color::Good);
                y = ui::drawTagRow(left, y, 108, "Time zone",
                                   tz::formatOffset(offset) +
                                       (zone >= 0 ? std::string(", ") +
                                                        tz::zones()[zone].places
                                                  : std::string()),
                                   ui::color::Neutral);
                ui::drawWrapped(wrong ? "The clock is " +
                                            tz::describeDrift(console, correct) +
                                            ". Secure downloads fail until it "
                                            "is set."
                                      : "The clock is right.",
                                200, y + 6, ui::type::Small,
                                wrong ? ui::color::rgb(0xB84A1A)
                                      : ui::color::Good,
                                330, 2, C2D_AlignCenter);
            };
            spec.drawBottom = [&]() {
                ui::drawWrapped("Setting the clock needs the CIA version of "
                                "3dsDL. Rosalina can also set the time.",
                                160, 136, ui::type::Tiny, ui::color::InkSoft,
                                260, 3, C2D_AlignCenter);
            };
            int focus = wrong ? 0 : 1;
            const int choice = ui::runScreen(spec, focus).choice;
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
    ui::Dialog dialog;
    dialog.header = "3dsDL " APP_VERSION;
    dialog.title = "3dsDL " APP_VERSION;
    dialog.body.push_back({"Download files straight onto your Nintendo 3DS "
                           "from a link or a QR code. Settings and history "
                           "live in " +
                               std::string(storage::DataFolder) + ".",
                           ui::color::Ink, 0.5f, 6});
    dialog.body.push_back({"GPLv3. Built on libcurl, Mbed TLS, quirc and "
                           "Citro2D, and grew out of Multidownload by "
                           "hax0kartik. CIA installing follows FBI by "
                           "Steveice10 (MIT).",
                           ui::color::InkSoft, 0.5f, 3});
    dialog.buttons = {"OK", "Report an Issue"};
    dialog.cancelButton = 0;
    if (ui::choose(dialog) == 1) {
        reportScreen();
    }
}
