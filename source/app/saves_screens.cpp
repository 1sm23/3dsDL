#include "app.hpp"
#include "i18n.hpp"

#include "app_util.hpp"
#include "file_ops.hpp"
#include "fs_util.hpp"
#include "naming.hpp"
#include "platform.hpp"
#include "saves.hpp"
#include "savedata.hpp"
#include "tasks.hpp"
#include "text.hpp"
#include "ui.hpp"
#include "units.hpp"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <set>

// Save data and extra data: backups in Checkpoint's folders, restoring
// them, and looking inside.

namespace {

using screens::plural;

constexpr const char *ExportFolder = "sdmc:/3ds/3dsDL/exported/";

std::string nowName()
{
    const time_t seconds = static_cast<time_t>(platform::now());
    std::tm parts {};
    gmtime_r(&seconds, &parts); // the console clock is already local time
    return saves::backupName(parts);
}

const char *kindTitle(savedata::Kind kind)
{
    return kind == savedata::Kind::Save ? i18n::c("Save Data") : i18n::c("Extra Data");
}

ui::Icon kindIcon(savedata::Kind kind)
{
    return kind == savedata::Kind::Save ? ui::Icon::Chip : ui::Icon::Folder;
}

u32 kindColor(savedata::Kind kind)
{
    return kind == savedata::Kind::Save ? ui::color::IconTeal
                                        : ui::color::IconOrange;
}

struct Quiet : fileops::Observer {
    bool cancelRequested() override { return false; }
};

// Files and folders alike, so a copy never lands on either.
bool taken(const std::string &path)
{
    return fileops::exists(path);
}

} // namespace

namespace {

// Looks for save data on a worker thread, one title at a time, so the Save
// Data list fills in while it looks. Each title is looked at once.
class SaveSearch {
public:
    struct Found {
        titles::Title title;
        size_t backups = 0;
    };

    SaveSearch()
    {
        LightLock_Init(&lock_);
        thread_ = tasks::startWorker(run, this, "saves");
    }
    ~SaveSearch()
    {
        stop_ = true;
        if (thread_ != nullptr) {
            threadJoin(thread_, U64_MAX);
            threadFree(thread_);
        }
    }
    SaveSearch(const SaveSearch &) = delete;
    SaveSearch &operator=(const SaveSearch &) = delete;

    void add(const titles::Title &title)
    {
        if (thread_ == nullptr) {
            look(title); // no thread to be had: look now
            return;
        }
        ++waiting_;
        LightLock_Lock(&lock_);
        queue_.push_back(title);
        LightLock_Unlock(&lock_);
    }
    // Moves what was found into out; true when anything was.
    bool take(std::vector<Found> &out)
    {
        LightLock_Lock(&lock_);
        const bool any = !found_.empty();
        for (Found &found : found_) {
            out.push_back(std::move(found));
        }
        found_.clear();
        LightLock_Unlock(&lock_);
        return any;
    }
    bool busy() const { return waiting_ > 0; }

private:
    static void run(void *argument)
    {
        SaveSearch &search = *static_cast<SaveSearch *>(argument);
        while (!search.stop_) {
            LightLock_Lock(&search.lock_);
            const bool any = !search.queue_.empty();
            titles::Title title;
            if (any) {
                title = std::move(search.queue_.front());
                search.queue_.erase(search.queue_.begin());
            }
            LightLock_Unlock(&search.lock_);
            if (!any) {
                svcSleepThread(20 * 1000 * 1000);
                continue;
            }
            search.look(title);
            --search.waiting_;
        }
    }

    // A title has save data when its save archive opens.
    void look(const titles::Title &title)
    {
        const savedata::Target target =
            savedata::saveOf(title, library::mediaOf(title));
        if (!savedata::exists(target)) {
            return;
        }
        Found found {title, savedata::backups(target).size()};
        LightLock_Lock(&lock_);
        found_.push_back(std::move(found));
        LightLock_Unlock(&lock_);
    }

    Thread thread_ = nullptr;
    std::atomic<bool> stop_ {false};
    std::atomic<int> waiting_ {0};
    LightLock lock_;
    // Guarded by lock_.
    std::vector<titles::Title> queue_;
    std::vector<Found> found_;
};

} // namespace

void App::saveTitlesScreen()
{
    // Titles are handed to the search as the library reads them, so the
    // list fills in while both are still going.
    SaveSearch search;
    std::set<uint64_t> queued;
    std::vector<SaveSearch::Found> withSaves;
    titles::Index index;
    uint32_t seen = libraryGeneration_ - 1;

    int cursor = 0;
    while (ui::keepRunning() && !quit_) {
        ui::ListSpec spec;
        spec.title = i18n::c("Save Data");
        spec.emptyText = i18n::c("No save data.");
        bool rebuild = true; // once more on each return to the list
        spec.update = [&]() {
            if (seen != libraryGeneration_) {
                seen = libraryGeneration_;
                index = titles::indexOf(library_.titles);
                for (const titles::Title &title : library_.titles) {
                    if (titles::isSoftware(title.id) &&
                        savedata::unsupportedReason(title).empty() &&
                        queued.insert(title.id).second) {
                        search.add(title);
                    }
                }
                rebuild = true;
            }
            if (search.take(withSaves)) {
                rebuild = true;
            }
            if (!rebuild) {
                return false;
            }
            rebuild = false;
            std::stable_sort(withSaves.begin(), withSaves.end(),
                             [&](const SaveSearch::Found &a,
                                 const SaveSearch::Found &b) {
                                 return text::lower(titles::displayName(a.title, index)) <
                                        text::lower(titles::displayName(b.title, index));
                             });
            spec.items.clear();
            for (const SaveSearch::Found &found : withSaves) {
                ui::ListItem item;
                item.key = cia::titleIdText(found.title.id);
                item.label = titles::displayName(found.title, index);
                item.icon = found.title.place == titles::Place::GameCard
                                ? ui::Icon::GameCard
                                : ui::Icon::Chip;
                item.iconColor = ui::color::IconTeal;
                item.detail =
                    found.backups == 0 ? "" : plural(found.backups, "backup");
                spec.items.push_back(item);
            }
            return true;
        };
        spec.update();
        spec.loading = [&]() {
            std::string ignored;
            return (!library_.complete && !libraryLoader_.failed(ignored)) ||
                   search.busy();
        };
        spec.drawTop = [&](int highlighted) {
            ui::drawTopChrome(i18n::c("Save Data"), ui::Icon::Chip, ui::color::IconTeal);
            ui::drawPanel();
            if (highlighted < 0 || highlighted >= static_cast<int>(withSaves.size())) {
                ui::drawEmblem(ui::Icon::Chip, ui::color::IconTeal);
                return;
            }
            const titles::Title &title = withSaves[highlighted].title;
            const float left = ui::TopPanel.x + 18;
            const float top = ui::TopPanel.y + 12;
            ui::drawTitleIcon(title.icon, left, top, ui::Icon::Chip);
            ui::drawMarquee(titles::displayName(title, index), left + 60, top + 2,
                            0.62f, ui::color::InkStrong, 280);
            ui::drawMarquee(title.publisher, left + 60, top + 30, ui::type::Small,
                            ui::color::InkSoft, 280);
            const float y = ui::drawTagRow(left, top + 60, 96, i18n::c("Title ID"),
                                           cia::titleIdText(title.id));
            ui::drawTagRow(left, y, 96, i18n::c("Backups in"),
                           savedata::saveOf(title, library::mediaOf(title)).folder);
        };
        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key != KEY_A || result.index < 0 ||
            result.index >= static_cast<int>(withSaves.size())) {
            return;
        }
        SaveSearch::Found &found = withSaves[result.index];
        const savedata::Target target =
            savedata::saveOf(found.title, library::mediaOf(found.title));
        saveDataScreen(target, found.title.icon);
        // Backing up or deleting there changes the count shown here.
        found.backups = savedata::backups(target).size();
    }
}

void App::saveDataScreen(const savedata::Target &target,
                         const std::vector<uint16_t> &icon)
{
    const std::string what = target.kind == savedata::Kind::Save ? i18n::c("save data")
                                                                  : i18n::c("extra data");
    int cursor = 0;
    while (ui::keepRunning() && !quit_) {
        const std::vector<std::string> names = savedata::backups(target);
        ui::ListSpec spec;
        spec.title = target.name.empty() ? kindTitle(target.kind) : target.name;
        ui::ListItem fresh;
        fresh.label = i18n::c("New Backup");
        fresh.icon = ui::Icon::Download;
        fresh.iconColor = ui::color::IconGreen;
        fresh.labelColor = ui::color::Accent;
        spec.items.push_back(fresh);
        for (const std::string &name : names) {
            ui::ListItem item;
            item.label = name;
            item.icon = ui::Icon::History;
            item.iconColor = kindColor(target.kind);
            spec.items.push_back(item);
        }
        spec.action = i18n::c("Browse");
        spec.actionKey = "X";
        spec.actionKeys = KEY_X;
        spec.drawTop = [&](int) {
            ui::drawTopChrome(kindTitle(target.kind), kindIcon(target.kind),
                              kindColor(target.kind));
            ui::drawPanel();
            const float left = ui::TopPanel.x + 18;
            const float top = ui::TopPanel.y + 12;
            ui::drawTitleIcon(icon, left, top, kindIcon(target.kind));
            ui::drawMarquee(target.name.empty() ? kindTitle(target.kind)
                                                : target.name,
                            left + 60, top + 2, 0.62f, ui::color::InkStrong, 280);
            ui::drawText(plural(names.size(), "backup"), left + 60, top + 30,
                         ui::type::Small, ui::color::InkSoft);
            ui::drawTagRow(left, top + 60, 76, i18n::c("Folder"), target.folder);
        };
        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key == KEY_X) {
            dataBrowser(target);
            continue;
        }
        if (result.key != KEY_A || result.index < 0) {
            return;
        }

        if (result.index == 0) {
            std::string typed;
            const std::string suggested = nowName();
            if (!platform::keyboard(platform::KeyboardKind::Name,
                                    i18n::c("Backup name (up to 20 characters)"),
                                    suggested, typed)) {
                continue;
            }
            std::string name = typed == suggested
                                   ? suggested
                                   : saves::customBackupName(text::trim(typed));
            if (name.empty()) {
                name = suggested;
            }
            if (std::find(names.begin(), names.end(), name) != names.end()) {
                ui::Dialog dialog;
                dialog.header = i18n::c("Backup");
                dialog.title = i18n::t("Replace the backup {0}?", {name});
                dialog.tone = ui::Tone::Warning;
                dialog.buttons = {i18n::c("Replace"), i18n::c("Cancel")};
                dialog.dangerous = 0;
                dialog.initial = 1;
                dialog.cancelButton = 1;
                if (ui::choose(dialog) != 0) {
                    continue;
                }
            }
            savedata::Progress progress;
            savedata::Result outcome;
            tasks::busy(i18n::c("Backup"), i18n::t("Backing up the {0}...", {what}),
                        [&]() { outcome = savedata::backup(target, name, progress); });
            if (outcome.ok) {
                ui::message(i18n::c("Backed Up"),
                            i18n::t("{0} saved as {1}.", {plural(outcome.files, "file"), name}),
                            ui::Tone::Success,
                            outcome.skipped > 0
                                ? i18n::t("{0} the system was using were left out.", {plural(outcome.skipped, "file")})
                                : target.folder + name);
            } else {
                ui::message(i18n::c("Not Backed Up"), outcome.message, ui::Tone::Error);
            }
            continue;
        }

        const std::string name = names[result.index - 1];
        const int choice = ui::menu(
            {{i18n::c("Restore"), true, true}, {i18n::c("Delete Backup"), true, true}},
            [&](int) {
                ui::drawTopChrome(name, kindIcon(target.kind), kindColor(target.kind));
                ui::drawPanel();
                ui::drawFacts({{i18n::c("Title"), target.name.empty()
                                             ? kindTitle(target.kind)
                                             : target.name},
                               {i18n::c("Folder"), target.folder + name + "/"}});
            });
        if (choice == 0) {
            // No body, so the backup's facts stay on the top screen.
            ui::Dialog dialog;
            dialog.header = i18n::c("Restore");
            dialog.title = i18n::t("Replace the {0} on the console with {1}?", {what, name});
            dialog.tone = ui::Tone::Warning;
            dialog.buttons = {i18n::c("Restore"), i18n::c("Cancel")};
            dialog.dangerous = 0;
            dialog.initial = 1;
            dialog.cancelButton = 1;
            if (ui::choose(dialog) != 0) {
                continue;
            }
            savedata::Progress progress;
            savedata::Result safety;
            savedata::Result outcome;
            const std::string safetyName = i18n::t("{0} Before Restore", {nowName()});
            tasks::busy(i18n::c("Restore"), i18n::c("Backing up what is there now..."), [&]() {
                safety = savedata::backup(target, safetyName, progress);
            });
            if (!safety.ok) {
                ui::Dialog warn;
                warn.header = i18n::c("Restore");
                warn.title = i18n::c("What is there now could not be backed up.");
                warn.tone = ui::Tone::Warning;
                warn.body.push_back({safety.message, ui::color::Text, 0.5f, 3});
                warn.buttons = {i18n::c("Restore Anyway"), i18n::c("Cancel")};
                warn.dangerous = 0;
                warn.initial = 1;
                warn.cancelButton = 1;
                if (ui::choose(warn) != 0) {
                    continue;
                }
            }
            tasks::busy(i18n::c("Restore"), i18n::t("Restoring {0}...", {name}), [&]() {
                outcome = savedata::restore(target, name, progress);
            });
            if (outcome.ok) {
                ui::message(i18n::c("Restored"),
                            i18n::t("{0} restored from {1}.", {plural(outcome.files, "file"), name}),
                            ui::Tone::Success,
                            safety.ok ? i18n::t("The earlier {0} is kept as {1}.", {what, safetyName})
                                      : "");
            } else {
                ui::message(i18n::c("Not Restored"), outcome.message, ui::Tone::Error,
                            safety.ok ? i18n::t("The earlier {0} is kept as {1}.", {what, safetyName})
                                      : "");
            }
        } else if (choice == 1) {
            ui::Dialog dialog;
            dialog.header = i18n::c("Delete");
            dialog.title = i18n::t("Delete the backup {0}?", {name});
            dialog.tone = ui::Tone::Warning;
            dialog.buttons = {i18n::c("Delete"), i18n::c("Cancel")};
            dialog.dangerous = 0;
            dialog.initial = 1;
            dialog.cancelButton = 1;
            if (ui::choose(dialog) == 0) {
                Quiet quiet;
                uint64_t done = 0;
                const fileops::Result removed =
                    fileops::remove(target.folder + name, quiet, done, 0);
                if (removed.status != fileops::Status::Ok) {
                    ui::message(i18n::c("Not Deleted"), removed.message, ui::Tone::Error);
                }
            }
        }
    }
}

void App::dataBrowser(const savedata::Target &target)
{
    const std::string device =
        target.kind == savedata::Kind::Save ? "save" : "extdata";
    std::string error;
    if (!savedata::mount(target, device, error)) {
        ui::message(i18n::c("Cannot Open"), error, ui::Tone::Error);
        return;
    }
    const std::string root = device + ":/";
    std::string folder = root;
    int cursor = 0;
    while (ui::keepRunning() && !quit_) {
        bool readable = false;
        std::vector<fs::Entry> entries = fs::listEntries(folder, &readable, true);
        fs::sortEntries(entries, fs::SortOrder::Name);
        ui::ListSpec spec;
        spec.title = target.name + "  " + folder.substr(root.size() - 1);
        spec.heading = spec.title;
        spec.tapToSelect = true;
        spec.back = folder == root ? i18n::c("Close") : i18n::c("Back");
        spec.emptyText = readable ? i18n::c("This folder is empty.") : i18n::c("This folder cannot be read.");
        for (const fs::Entry &entry : entries) {
            ui::ListItem item;
            item.label = entry.name;
            item.icon = entry.folder ? ui::Icon::Folder : ui::Icon::File;
            item.iconColor = entry.folder ? ui::color::IconOrange : ui::color::IconGrey;
            if (!entry.folder && entry.size >= 0) {
                item.detail = units::bytes(static_cast<uint64_t>(entry.size));
            }
            spec.items.push_back(item);
        }
        spec.action = i18n::c("Copy All");
        spec.actionKey = "X";
        spec.actionKeys = KEY_X;
        spec.drawTop = [&](int highlighted) {
            ui::drawTopChrome(kindTitle(target.kind), kindIcon(target.kind),
                              kindColor(target.kind));
            ui::drawPanel();
            const float left = ui::TopPanel.x + 18;
            float y = ui::TopPanel.y + 12;
            ui::drawMarquee(target.name, left, y, 0.62f, ui::color::InkStrong,
                            ui::TopPanel.w - 36);
            y += 32;
            y = ui::drawTagRow(left, y, 76, i18n::c("Folder"), folder.substr(root.size() - 1));
            if (highlighted >= 0 && highlighted < static_cast<int>(entries.size())) {
                const fs::Entry &entry = entries[highlighted];
                y = ui::drawTagRow(left, y, 76, entry.folder ? i18n::c("Folder") : i18n::c("File"),
                                   entry.name);
                if (!entry.folder && entry.size >= 0) {
                    ui::drawTagRow(left, y, 76, i18n::c("Size"),
                                   units::bytes(static_cast<uint64_t>(entry.size)));
                }
            }
        };
        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key == KEY_B || result.key == 0) {
            if (folder == root || result.key == 0) {
                break;
            }
            folder = naming::parentFolder(folder);
            cursor = 0;
            continue;
        }
        // What to copy out: one file, or everything in this folder.
        std::vector<std::string> sources;
        std::string into; // a new folder for Copy All, or "" for one file
        if (result.key == KEY_X) {
            for (const fs::Entry &entry : entries) {
                sources.push_back(folder + entry.name);
            }
            into = folder == root ? nowName()
                                  : fileops::name(folder.substr(0, folder.size() - 1));
            if (sources.empty()) {
                continue;
            }
        } else if (result.index >= 0 && result.index < static_cast<int>(entries.size())) {
            const fs::Entry &entry = entries[result.index];
            if (entry.folder) {
                folder += entry.name + "/";
                cursor = 0;
                continue;
            }
            const std::string file = folder + entry.name;
            const int choice = ui::menu(
                {{i18n::c("View as Hex"), false, true}, {i18n::c("Copy to SD Card"), false, true}},
                [&](int) {
                    ui::drawTopChrome(entry.name, ui::Icon::File, ui::color::IconGrey);
                    ui::drawPanel();
                    std::vector<ui::Fact> facts = {
                        {i18n::c("Folder"), folder.substr(root.size() - 1)}};
                    if (entry.size >= 0) {
                        facts.push_back(
                            {i18n::c("Size"), units::bytes(static_cast<uint64_t>(entry.size))});
                    }
                    ui::drawFacts(facts);
                });
            if (choice == 0) {
                hexViewer(file);
                continue;
            }
            if (choice != 1) {
                continue;
            }
            sources.push_back(file);
        } else {
            continue;
        }

        // Into a folder named after the title, unless another is chosen.
        std::string destination = std::string(ExportFolder) +
                                  saves::sanitize(target.name.empty()
                                                      ? kindTitle(target.kind)
                                                      : target.name) +
                                  "/";
        fs::makeDirectories(destination);
        if (!chooseFolder(destination)) {
            continue;
        }
        if (!into.empty()) {
            destination = naming::unusedPath(destination, into, taken) + "/";
            if (!fs::makeDirectories(destination)) {
                ui::message(i18n::c("Not Copied"), i18n::t("Could not create {0}.", {destination}),
                            ui::Tone::Error);
                continue;
            }
        }
        uint64_t total = 0;
        for (const std::string &source : sources) {
            total += fileops::measure(source).bytes;
        }
        std::string last;
        const fileops::Result copied = tasks::fileOperation(
            i18n::c("Copy"), i18n::c("Copying to the SD card"), destination, total, false,
            [&](fileops::Observer &observer) {
                uint64_t done = 0;
                fileops::Result outcome;
                for (const std::string &source : sources) {
                    last = naming::unusedPath(destination, fileops::name(source),
                                              taken);
                    outcome = fileops::copy(source, last, observer, done, total);
                    if (outcome.status != fileops::Status::Ok) {
                        break;
                    }
                }
                return outcome;
            },
            settings_.keepAwake);
        if (copied.status == fileops::Status::Ok) {
            ui::message(i18n::c("Copied"),
                        i18n::t("Copied to {0}.", {(into.empty() ? last : destination)}),
                        ui::Tone::Success);
        } else if (copied.status == fileops::Status::Failed) {
            ui::message(i18n::c("Not Copied"), copied.message, ui::Tone::Error);
        }
    }
    savedata::unmount(device);
}
