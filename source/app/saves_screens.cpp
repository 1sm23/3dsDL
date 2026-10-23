#include "app.hpp"

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
#include <ctime>

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
    return kind == savedata::Kind::Save ? "Save Data" : "Extra Data";
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

void App::saveTitlesScreen()
{
    // Which software has a save: its archive opens. Checked once.
    std::vector<titles::Title> withSaves;
    const bool looked = tasks::busy(
        "Save Data", "Looking for save data...",
        [&](tasks::Busy &busy) {
            busy.total = library_.titles.size();
            for (const titles::Title &title : library_.titles) {
                if (busy.cancel) {
                    return;
                }
                busy.done.fetch_add(1);
                if (!titles::isSoftware(title.id) ||
                    !savedata::unsupportedReason(title).empty()) {
                    continue;
                }
                if (savedata::exists(
                        savedata::saveOf(title, library::mediaOf(title)))) {
                    withSaves.push_back(title);
                }
            }
        },
        true);
    if (!looked) {
        return;
    }
    const titles::Index index = titles::indexOf(library_.titles);
    std::stable_sort(withSaves.begin(), withSaves.end(),
                     [&](const titles::Title &a, const titles::Title &b) {
                         return text::lower(titles::displayName(a, index)) <
                                text::lower(titles::displayName(b, index));
                     });

    int cursor = 0;
    while (ui::keepRunning() && !quit_) {
        ui::ListSpec spec;
        spec.title = "Save Data";
        spec.emptyText = "No save data.";
        for (const titles::Title &title : withSaves) {
            ui::ListItem item;
            item.label = titles::displayName(title, index);
            item.icon = title.place == titles::Place::GameCard
                            ? ui::Icon::GameCard
                            : ui::Icon::Chip;
            item.iconColor = ui::color::IconTeal;
            const size_t count =
                savedata::backups(savedata::saveOf(title, library::mediaOf(title)))
                    .size();
            item.detail = count == 0 ? "" : plural(count, "backup");
            spec.items.push_back(item);
        }
        spec.drawTop = [&](int highlighted) {
            ui::drawTopChrome("Save Data", ui::Icon::Chip, ui::color::IconTeal);
            ui::drawPanel();
            if (highlighted < 0 || highlighted >= static_cast<int>(withSaves.size())) {
                ui::drawEmblem(ui::Icon::Chip, ui::color::IconTeal);
                return;
            }
            const titles::Title &title = withSaves[highlighted];
            const float left = ui::TopPanel.x + 18;
            const float top = ui::TopPanel.y + 12;
            ui::drawTitleIcon(title.icon, left, top, ui::Icon::Chip);
            ui::drawMarquee(titles::displayName(title, index), left + 60, top + 2,
                            0.62f, ui::color::InkStrong, 280);
            ui::drawMarquee(title.publisher, left + 60, top + 30, ui::type::Small,
                            ui::color::InkSoft, 280);
            const float y = ui::drawTagRow(left, top + 60, 96, "Title ID",
                                           cia::titleIdText(title.id));
            ui::drawTagRow(left, y, 96, "Backups in",
                           savedata::saveOf(title, library::mediaOf(title)).folder);
        };
        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key != KEY_A || result.index < 0 ||
            result.index >= static_cast<int>(withSaves.size())) {
            return;
        }
        const titles::Title &title = withSaves[result.index];
        saveDataScreen(savedata::saveOf(title, library::mediaOf(title)), title.icon);
    }
}

void App::saveDataScreen(const savedata::Target &target,
                         const std::vector<uint16_t> &icon)
{
    const std::string what = target.kind == savedata::Kind::Save ? "save data"
                                                                  : "extra data";
    int cursor = 0;
    while (ui::keepRunning() && !quit_) {
        const std::vector<std::string> names = savedata::backups(target);
        ui::ListSpec spec;
        spec.title = target.name.empty() ? kindTitle(target.kind) : target.name;
        ui::ListItem fresh;
        fresh.label = "New Backup";
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
        spec.action = "Browse";
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
            ui::drawTagRow(left, top + 60, 76, "Folder", target.folder);
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
                                    "Backup name (up to 20 characters)",
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
                dialog.header = "Backup";
                dialog.title = "Replace the backup " + name + "?";
                dialog.tone = ui::Tone::Warning;
                dialog.buttons = {"Replace", "Cancel"};
                dialog.dangerous = 0;
                dialog.initial = 1;
                dialog.cancelButton = 1;
                if (ui::choose(dialog) != 0) {
                    continue;
                }
            }
            savedata::Progress progress;
            savedata::Result outcome;
            tasks::busy("Backup", "Backing up the " + what + "...",
                        [&]() { outcome = savedata::backup(target, name, progress); });
            if (outcome.ok) {
                ui::message("Backed Up",
                            plural(outcome.files, "file") + " saved as " + name +
                                ".",
                            ui::Tone::Success,
                            outcome.skipped > 0
                                ? plural(outcome.skipped, "file") +
                                      " the system was using were left out."
                                : target.folder + name);
            } else {
                ui::message("Not Backed Up", outcome.message, ui::Tone::Error);
            }
            continue;
        }

        const std::string name = names[result.index - 1];
        const int choice = ui::menu(
            {{"Restore", true, true}, {"Delete Backup", true, true}},
            [&](int) {
                ui::drawTopChrome(name, kindIcon(target.kind), kindColor(target.kind));
                ui::drawPanel();
                ui::drawFacts({{"Title", target.name.empty()
                                             ? kindTitle(target.kind)
                                             : target.name},
                               {"Folder", target.folder + name + "/"}});
            });
        if (choice == 0) {
            // No body, so the backup's facts stay on the top screen.
            ui::Dialog dialog;
            dialog.header = "Restore";
            dialog.title = "Replace the " + what + " on the console with " +
                           name + "?";
            dialog.tone = ui::Tone::Warning;
            dialog.buttons = {"Restore", "Cancel"};
            dialog.dangerous = 0;
            dialog.initial = 1;
            dialog.cancelButton = 1;
            if (ui::choose(dialog) != 0) {
                continue;
            }
            savedata::Progress progress;
            savedata::Result safety;
            savedata::Result outcome;
            const std::string safetyName = nowName() + " Before Restore";
            tasks::busy("Restore", "Backing up what is there now...", [&]() {
                safety = savedata::backup(target, safetyName, progress);
            });
            if (!safety.ok) {
                ui::Dialog warn;
                warn.header = "Restore";
                warn.title = "What is there now could not be backed up.";
                warn.tone = ui::Tone::Warning;
                warn.body.push_back({safety.message, ui::color::Text, 0.5f, 3});
                warn.buttons = {"Restore Anyway", "Cancel"};
                warn.dangerous = 0;
                warn.initial = 1;
                warn.cancelButton = 1;
                if (ui::choose(warn) != 0) {
                    continue;
                }
            }
            tasks::busy("Restore", "Restoring " + name + "...", [&]() {
                outcome = savedata::restore(target, name, progress);
            });
            if (outcome.ok) {
                ui::message("Restored",
                            plural(outcome.files, "file") + " restored from " +
                                name + ".",
                            ui::Tone::Success,
                            safety.ok ? "The earlier " + what + " is kept as " +
                                            safetyName + "."
                                      : "");
            } else {
                ui::message("Not Restored", outcome.message, ui::Tone::Error,
                            safety.ok ? "The earlier " + what + " is kept as " +
                                            safetyName + "."
                                      : "");
            }
        } else if (choice == 1) {
            ui::Dialog dialog;
            dialog.header = "Delete";
            dialog.title = "Delete the backup " + name + "?";
            dialog.tone = ui::Tone::Warning;
            dialog.buttons = {"Delete", "Cancel"};
            dialog.dangerous = 0;
            dialog.initial = 1;
            dialog.cancelButton = 1;
            if (ui::choose(dialog) == 0) {
                Quiet quiet;
                uint64_t done = 0;
                const fileops::Result removed =
                    fileops::remove(target.folder + name, quiet, done, 0);
                if (removed.status != fileops::Status::Ok) {
                    ui::message("Not Deleted", removed.message, ui::Tone::Error);
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
        ui::message("Cannot Open", error, ui::Tone::Error);
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
        spec.back = folder == root ? "Close" : "Back";
        spec.emptyText = readable ? "This folder is empty." : "This folder cannot be read.";
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
        spec.action = "Copy All";
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
            y = ui::drawTagRow(left, y, 76, "Folder", folder.substr(root.size() - 1));
            if (highlighted >= 0 && highlighted < static_cast<int>(entries.size())) {
                const fs::Entry &entry = entries[highlighted];
                y = ui::drawTagRow(left, y, 76, entry.folder ? "Folder" : "File",
                                   entry.name);
                if (!entry.folder && entry.size >= 0) {
                    ui::drawTagRow(left, y, 76, "Size",
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
                {{"View as Hex", false, true}, {"Copy to SD Card", false, true}},
                [&](int) {
                    ui::drawTopChrome(entry.name, ui::Icon::File, ui::color::IconGrey);
                    ui::drawPanel();
                    std::vector<ui::Fact> facts = {
                        {"Folder", folder.substr(root.size() - 1)}};
                    if (entry.size >= 0) {
                        facts.push_back(
                            {"Size", units::bytes(static_cast<uint64_t>(entry.size))});
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
                ui::message("Not Copied", "Could not create " + destination + ".",
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
            "Copy", "Copying to the SD card", destination, total, false,
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
            ui::message("Copied",
                        "Copied to " + (into.empty() ? last : destination) + ".",
                        ui::Tone::Success);
        } else if (copied.status == fileops::Status::Failed) {
            ui::message("Not Copied", copied.message, ui::Tone::Error);
        }
    }
    savedata::unmount(device);
}
