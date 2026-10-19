#include "app.hpp"

#include "app_util.hpp"
#include "fs_util.hpp"
#include "installer.hpp"
#include "library.hpp"
#include "tasks.hpp"
#include "ui.hpp"
#include "text.hpp"
#include "units.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

// Titles: installed software, updates and DLC, extra data, tickets, seeds
// and cleaning up after failed installs, in the spirit of System Settings,
// Data Management.

namespace {

using screens::plural;

// Where a seeddb.bin is usually left: by hand, by GodMode9, or for 3dsDL.
const char *const SeedDbPlaces[] = {
    "sdmc:/seeddb.bin",
    "sdmc:/3ds/seeddb.bin",
    "sdmc:/3ds/3dsDL/seeddb.bin",
    "sdmc:/gm9/out/seeddb.bin",
};

const char *placeName(titles::Place place)
{
    return place == titles::Place::SystemMemory ? "system memory" : "SD card";
}

ui::Icon iconFor(uint64_t titleId)
{
    switch (cia::kind(titleId)) {
    case cia::Kind::Update:
        return ui::Icon::Download;
    case cia::Kind::Dlc:
        return ui::Icon::Package;
    case cia::Kind::DsiWare:
        return ui::Icon::App;
    default:
        return ui::Icon::GameCard;
    }
}

// The title's icon, name and publisher over its Title ID, version and size.
void drawTitleTop(const std::string &header, ui::Icon headerIcon,
                  const titles::Title &title, const titles::Index &index)
{
    ui::drawTopChrome(header, headerIcon, ui::color::IconBlue);
    ui::drawPanel();
    const float left = ui::TopPanel.x + 18;
    const float top = ui::TopPanel.y + 12;
    const titles::Title *game = &title;
    if (title.icon.empty()) {
        const auto found = index.find(cia::baseTitle(title.id));
        if (found != index.end()) {
            game = found->second;
        }
    }
    ui::drawTitleIcon(game->icon, left, top, iconFor(title.id));
    ui::drawText(ui::fitEnd(titles::displayName(title, index), 0.62f, 280),
                 left + 60, top + 2, 0.62f, ui::color::InkStrong);
    std::string second = game->publisher.empty()
                             ? cia::kindName(cia::kind(title.id))
                             : game->publisher;
    if (!title.productCode.empty()) {
        second += "   " + title.productCode;
    }
    ui::drawText(ui::fitEnd(second, ui::type::Small, 280), left + 60, top + 30,
                 ui::type::Small, ui::color::InkSoft);
    float y = top + 60;
    y = ui::drawTagRow(left, y, 76, "Title ID", cia::titleIdText(title.id));
    y = ui::drawTagRow(left, y, 76, "Version", cia::versionText(title.version));
    ui::drawTagRow(left, y, 76, "Size",
                   units::bytes(title.size) + ", " + placeName(title.place));
}

bool confirmDanger(const std::string &header, const std::string &title,
                   const std::vector<ui::Paragraph> &body,
                   const std::string &button,
                   const std::vector<uint16_t> &icon = {})
{
    ui::Dialog dialog;
    dialog.header = header;
    dialog.title = title;
    dialog.tone = ui::Tone::Warning;
    dialog.body = body;
    dialog.icon = icon.empty() ? nullptr : ui::showIcon(icon);
    dialog.buttons = {button, "Cancel"};
    dialog.dangerous = 0;
    dialog.initial = 1;
    dialog.cancelButton = 1;
    return ui::choose(dialog) == 0;
}

const library::ExtData *extDataFor(const titles::Title &title,
                                   const library::Snapshot &snapshot)
{
    for (const library::ExtData &extData : snapshot.extData) {
        if (extData.id == titles::uniqueId(title.id)) {
            return &extData;
        }
    }
    return nullptr;
}

} // namespace

bool App::reloadLibrary()
{
    std::string error;
    bool loaded = false;
    tasks::busy("Titles", "Reading the installed software...",
                [&]() { loaded = library::load(library_, error); });
    if (!loaded) {
        ui::message("Titles Unavailable", error, ui::Tone::Error);
    }
    return loaded;
}

void App::titlesScreen()
{
    if (!reloadLibrary()) {
        return;
    }
    static const char *const Help[] = {
        "Remove unfinished installs, which stop software from installing "
        "again, and tickets for software that is gone.",
        "Newer games need a seed to start. Import seeds from a seeddb.bin on "
        "the SD card for the games installed here.",
        "Games, apps, demos and DSiWare. Launch them, get a game's seed, or "
        "delete them.",
        "Updates and add-on content, listed under their game's name.",
        "Data that software keeps outside its save, such as downloaded "
        "content, courses or StreetPass data.",
        "Tickets let installed software start. An unused ticket belongs to "
        "software that is no longer installed.",
    };
    int focus = 2;
    while (ui::keepRunning() && !quit_) {
        const titles::Index index = titles::indexOf(library_.titles);
        size_t software = 0;
        size_t addOns = 0;
        uint64_t sdBytes = 0;
        for (const titles::Title &title : library_.titles) {
            (titles::isSoftware(title.id) ? software : addOns) += 1;
            if (title.place == titles::Place::SdCard) {
                sdBytes += title.size;
            }
        }
        const size_t unused =
            titles::unusedTickets(library_.tickets, library_.titles).size();
        const size_t cleanUp = unused + library_.pending.size();

        ui::ScreenSpec spec;
        spec.back = "Back";
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
        add({16, 6, 140, 26}, "Clean Up", ui::Icon::Trash, ui::color::IconRed,
            false);
        if (cleanUp > 0) {
            spec.controls.back().badge = std::to_string(cleanUp);
        }
        add({164, 6, 140, 26}, "Seeds", ui::Icon::Chip, ui::color::IconGold,
            false);
        add({16, 42, 140, 78}, "Software", ui::Icon::GameCard,
            ui::color::IconBlue, true);
        add({164, 42, 140, 78}, "Updates and DLC", ui::Icon::Download,
            ui::color::IconGreen, true);
        add({16, 127, 140, 76}, "Extra Data", ui::Icon::Folder,
            ui::color::IconOrange, true);
        add({164, 127, 140, 76}, "Tickets", ui::Icon::Lock, ui::color::IconGrey,
            true);
        spec.drawTop = [&]() {
            ui::drawTopChrome("Titles", ui::Icon::GameCard, ui::color::IconBlue);
            ui::drawPanel();
            const int shown = focus >= 0 && focus < 6 ? focus : 2;
            const float left = ui::TopPanel.x + 18;
            float y = ui::drawWrapped(Help[shown], left, ui::TopPanel.y + 12,
                                      ui::type::Small, ui::color::Ink,
                                      ui::TopPanel.w - 36, 3) +
                      10;
            switch (shown) {
            case 0:
                y = ui::drawTagRow(left, y, 110, "Unfinished",
                                   plural(library_.pending.size(), "install"));
                ui::drawTagRow(left, y, 110, "Unused",
                               plural(unused, "ticket"));
                break;
            case 1:
                ui::drawTagRow(left, y, 110, "Looks in", "sdmc:/seeddb.bin");
                break;
            case 2:
                y = ui::drawTagRow(left, y, 110, "Installed",
                                   plural(software, "title"));
                ui::drawTagRow(left, y, 110, "On SD card",
                               units::bytes(sdBytes));
                break;
            case 3:
                ui::drawTagRow(left, y, 110, "Installed",
                               plural(addOns, "item"));
                break;
            case 4:
                ui::drawTagRow(left, y, 110, "On SD card",
                               plural(library_.extData.size(), "item"));
                break;
            default:
                y = ui::drawTagRow(left, y, 110, "Tickets",
                                   std::to_string(library_.tickets.size()));
                ui::drawTagRow(left, y, 110, "Unused", std::to_string(unused));
                break;
            }
        };
        const ui::ScreenResult result = ui::runScreen(spec, focus);
        switch (result.choice) {
        case 0:
            cleanUpScreen();
            break;
        case 1:
            importSeeds();
            break;
        case 2:
            titleList(true);
            break;
        case 3:
            titleList(false);
            break;
        case 4:
            extraDataScreen();
            break;
        case 5:
            ticketsScreen();
            break;
        default:
            return;
        }
    }
}

void App::titleList(bool software)
{
    int cursor = 0;
    while (ui::keepRunning() && !quit_) {
        const titles::Index index = titles::indexOf(library_.titles);
        std::vector<const titles::Title *> shown;
        for (const titles::Title &title : library_.titles) {
            if (titles::isSoftware(title.id) == software) {
                shown.push_back(&title);
            }
        }
        titles::sortByName(shown, index);

        ui::ListSpec spec;
        spec.title = software ? "Software" : "Updates and DLC";
        spec.emptyText = software ? "No software is installed."
                                  : "No updates or DLC are installed.";
        for (const titles::Title *title : shown) {
            ui::ListItem item;
            item.label = titles::displayName(*title, index);
            item.icon = iconFor(title->id);
            item.iconColor = software ? ui::color::IconBlue : ui::color::IconGreen;
            const cia::Kind kind = cia::kind(title->id);
            if (kind == cia::Kind::Update || kind == cia::Kind::Dlc) {
                item.badge = kind == cia::Kind::Update ? "Update" : "DLC";
                item.badgeColor = kind == cia::Kind::Update ? ui::color::Tag
                                                            : ui::color::Good;
            } else {
                item.detail = units::bytes(title->size);
            }
            spec.items.push_back(item);
        }
        spec.drawTop = [&](int highlighted) {
            if (highlighted < 0 || highlighted >= static_cast<int>(shown.size())) {
                ui::drawTopChrome(spec.title, ui::Icon::GameCard,
                                  ui::color::IconBlue);
                ui::drawPanel();
                ui::drawPanelText(spec.emptyText, ui::TopPanel);
                return;
            }
            drawTitleTop(spec.title, software ? ui::Icon::GameCard
                                              : ui::Icon::Download,
                         *shown[highlighted], index);
        };
        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key != KEY_A || result.index < 0 ||
            result.index >= static_cast<int>(shown.size())) {
            return;
        }
        titleOptions(*shown[result.index]);
    }
}

void App::titleOptions(titles::Title title)
{
    const titles::Index index = titles::indexOf(library_.titles);
    const std::string name = titles::displayName(title, index);
    const cia::Kind kind = cia::kind(title.id);
    const bool launchable = kind == cia::Kind::Application ||
                            kind == cia::Kind::Demo ||
                            kind == cia::Kind::DsiWare;
    const library::ExtData *extData = extDataFor(title, library_);
    const bool self = title.id == library::runningTitle();

    enum Choice { Launch, Seed, ExtraData, Delete, DeleteWithTicket };
    std::vector<Choice> choices;
    std::vector<ui::MenuItem> items;
    const auto offer = [&](Choice choice, const std::string &label,
                           bool danger = false, bool enabled = true) {
        choices.push_back(choice);
        items.push_back({label, danger, enabled});
    };
    if (launchable) {
        offer(Launch, "Launch", false, !self);
    }
    if (kind == cia::Kind::Application) {
        offer(Seed, "Get Seed");
    }
    if (extData != nullptr) {
        offer(ExtraData, "Delete Extra Data", true);
    }
    offer(Delete, "Delete", true, !self);
    offer(DeleteWithTicket, "Delete with Ticket", true, !self);

    const int picked = ui::menu(items, [&](int) {
        drawTitleTop(name, iconFor(title.id), title, index);
    });
    if (picked < 0) {
        return;
    }

    std::string error;
    switch (choices[picked]) {
    case Launch:
        if (!installer::launch(title.id, error)) {
            ui::message("Could Not Launch", error, ui::Tone::Error);
        }
        return;
    case Seed: {
        std::string message;
        bool added = false;
        tasks::busy("Seeds", "Getting the seed " + name + " needs...",
                    [&]() { added = installer::installSeed(title.id, message); });
        ui::message(added ? "Seed Added" : "No Seed Added", message,
                    added ? ui::Tone::Success : ui::Tone::Warning);
        return;
    }
    case ExtraData:
        if (confirmDanger("Delete",
                          "Delete the extra data of " + name + "?",
                          {{"Downloaded content, courses, StreetPass data and "
                            "other things " + name +
                                " keeps outside its save go. The save itself "
                                "stays.",
                            ui::color::Text, 0.5f, 4}},
                          "Delete", title.icon)) {
            if (!library::deleteExtData(extData->id, error)) {
                ui::message("Not Deleted", error, ui::Tone::Error);
            }
            reloadLibrary();
        }
        return;
    case Delete:
    case DeleteWithTicket: {
        const bool withTicket = choices[picked] == DeleteWithTicket;
        std::vector<ui::Paragraph> body;
        if (kind == cia::Kind::Update) {
            body.push_back({"The game stays installed and goes back to the "
                            "version it came with.",
                            ui::color::Text, 0.5f, 3});
        } else if (kind == cia::Kind::Dlc) {
            body.push_back({"The game stays installed without its add-on "
                            "content.",
                            ui::color::Text, 0.5f, 3});
        } else {
            body.push_back({"Its save data goes with it. Back the save up "
                            "with Checkpoint first if you want to keep it.",
                            ui::color::Warning, 0.5f, 3});
        }
        if (withTicket) {
            body.push_back({"Its ticket is deleted too.", ui::color::Muted,
                            0.46f, 2});
        }
        if (!confirmDanger("Delete", "Delete " + name + "?", body, "Delete",
                           title.icon)) {
            return;
        }
        bool deleted = false;
        tasks::busy("Delete", "Deleting " + name + "...", [&]() {
            deleted = library::deleteTitle(title, withTicket, error);
        });
        if (!deleted) {
            ui::message("Not Deleted", error, ui::Tone::Error);
        }
        reloadLibrary();
        return;
    }
    }
}

void App::extraDataScreen()
{
    int cursor = 0;
    while (ui::keepRunning() && !quit_) {
        const titles::Index index = titles::indexOf(library_.titles);
        const auto nameOf = [&](const library::ExtData &extData) {
            if (!extData.name.empty()) {
                return extData.name;
            }
            const titles::Title *owner =
                titles::ownerOfExtData(extData.id, library_.titles);
            if (owner != nullptr) {
                return titles::displayName(*owner, index);
            }
            char text[24];
            std::snprintf(text, sizeof(text), "Extra data %08llX",
                          static_cast<unsigned long long>(extData.id));
            return std::string(text);
        };
        std::vector<const library::ExtData *> shown;
        for (const library::ExtData &extData : library_.extData) {
            shown.push_back(&extData);
        }
        std::stable_sort(shown.begin(), shown.end(),
                         [&](const library::ExtData *a, const library::ExtData *b) {
                             return text::lower(nameOf(*a)) < text::lower(nameOf(*b));
                         });

        ui::ListSpec spec;
        spec.title = "Extra Data";
        spec.emptyText = "No extra data on the SD card.";
        for (const library::ExtData *extData : shown) {
            ui::ListItem item;
            item.label = nameOf(*extData);
            item.icon = ui::Icon::Folder;
            item.iconColor = ui::color::IconOrange;
            if (titles::ownerOfExtData(extData->id, library_.titles) == nullptr) {
                item.badge = "Unused";
                item.badgeColor = ui::color::Neutral;
            }
            spec.items.push_back(item);
        }
        spec.drawTop = [&](int highlighted) {
            ui::drawTopChrome("Extra Data", ui::Icon::Folder,
                              ui::color::IconOrange);
            ui::drawPanel();
            if (highlighted < 0 || highlighted >= static_cast<int>(shown.size())) {
                ui::drawPanelText(spec.emptyText, ui::TopPanel);
                return;
            }
            const library::ExtData &extData = *shown[highlighted];
            const titles::Title *owner =
                titles::ownerOfExtData(extData.id, library_.titles);
            const float left = ui::TopPanel.x + 18;
            const float top = ui::TopPanel.y + 12;
            ui::drawTitleIcon(!extData.icon.empty() || owner == nullptr
                                  ? extData.icon
                                  : owner->icon,
                              left, top, ui::Icon::Folder);
            ui::drawText(ui::fitEnd(nameOf(extData), 0.62f, 280), left + 60,
                         top + 2, 0.62f, ui::color::InkStrong);
            ui::drawText(ui::fitEnd(extData.publisher, ui::type::Small, 280),
                         left + 60, top + 30, ui::type::Small,
                         ui::color::InkSoft);
            char id[16];
            std::snprintf(id, sizeof(id), "%08llX",
                          static_cast<unsigned long long>(extData.id));
            float y = ui::drawTagRow(left, top + 60, 96, "ID", id);
            y = ui::drawTagRow(left, y, 96, "Software",
                               owner != nullptr ? "Installed"
                                                : "Not installed",
                               owner != nullptr ? ui::color::Tag
                                                : ui::color::Neutral);
            ui::drawWrapped("Delete removes what the software keeps outside "
                            "its save. The save itself stays.",
                            left, y + 2, ui::type::Tiny, ui::color::InkSoft,
                            ui::TopPanel.w - 36, 2);
        };
        spec.action = "Delete";
        spec.actionKey = "X";
        spec.actionKeys = KEY_X;
        const ui::ListResult result = ui::runList(spec, cursor);
        if ((result.key != KEY_A && result.key != KEY_X) || result.index < 0 ||
            result.index >= static_cast<int>(shown.size())) {
            return;
        }
        const library::ExtData extData = *shown[result.index];
        const std::string name = nameOf(extData);
        if (!confirmDanger("Delete", "Delete the extra data of " + name + "?",
                           {{"Downloaded content, courses, StreetPass data and "
                             "settings kept outside the save go. The save "
                             "itself stays.",
                             ui::color::Text, 0.5f, 4}},
                           "Delete", extData.icon)) {
            continue;
        }
        std::string error;
        if (!library::deleteExtData(extData.id, error)) {
            ui::message("Not Deleted", error, ui::Tone::Error);
        }
        reloadLibrary();
    }
}

void App::ticketsScreen()
{
    int cursor = 0;
    while (ui::keepRunning() && !quit_) {
        const titles::Index index = titles::indexOf(library_.titles);
        const std::vector<uint64_t> unusedList =
            titles::unusedTickets(library_.tickets, library_.titles);
        const std::set<uint64_t> unused(unusedList.begin(), unusedList.end());
        std::vector<uint64_t> shown = library_.tickets;
        std::stable_sort(shown.begin(), shown.end(), [&](uint64_t a, uint64_t b) {
            const std::string left = text::lower(titles::displayName(a, index));
            const std::string right = text::lower(titles::displayName(b, index));
            return left != right ? left < right : a < b;
        });

        ui::ListSpec spec;
        spec.title = "Tickets";
        spec.emptyText = "There are no tickets for user software.";
        for (const uint64_t ticket : shown) {
            ui::ListItem item;
            item.label = titles::displayName(ticket, index);
            item.icon = ui::Icon::Lock;
            item.iconColor = ui::color::IconGrey;
            if (unused.count(ticket) != 0) {
                item.badge = "Unused";
                item.badgeColor = ui::color::Neutral;
            } else {
                item.detail = cia::kindName(cia::kind(ticket));
            }
            spec.items.push_back(item);
        }
        spec.drawTop = [&](int highlighted) {
            ui::drawTopChrome("Tickets", ui::Icon::Lock, ui::color::IconGrey);
            ui::drawPanel();
            if (highlighted < 0 || highlighted >= static_cast<int>(shown.size())) {
                ui::drawPanelText(spec.emptyText, ui::TopPanel);
                return;
            }
            const uint64_t ticket = shown[highlighted];
            const auto own = index.find(ticket);
            const auto game = index.find(cia::baseTitle(ticket));
            const std::vector<uint16_t> noIcon;
            const std::vector<uint16_t> &icon =
                own != index.end() && !own->second->icon.empty() ? own->second->icon
                : game != index.end() ? game->second->icon
                                      : noIcon;
            const float left = ui::TopPanel.x + 18;
            const float top = ui::TopPanel.y + 12;
            ui::drawTitleIcon(icon, left, top, ui::Icon::Lock);
            ui::drawText(ui::fitEnd(titles::displayName(ticket, index), 0.62f, 280),
                         left + 60, top + 2, 0.62f, ui::color::InkStrong);
            ui::drawText(cia::kindName(cia::kind(ticket)), left + 60, top + 30,
                         ui::type::Small, ui::color::InkSoft);
            float y = ui::drawTagRow(left, top + 60, 96, "Title ID",
                                     cia::titleIdText(ticket));
            const bool isUnused = unused.count(ticket) != 0;
            y = ui::drawTagRow(left, y, 96, "Software",
                               isUnused ? "Not installed" : "Installed",
                               isUnused ? ui::color::Neutral : ui::color::Tag);
            ui::drawWrapped(isUnused
                                ? "Safe to delete. Installing a CIA again "
                                  "brings its ticket back."
                                : "The software will not start without its "
                                  "ticket.",
                            left, y + 2, ui::type::Tiny,
                            isUnused ? ui::color::InkSoft : ui::color::Caution,
                            ui::TopPanel.w - 36, 2);
        };
        if (!unusedList.empty()) {
            spec.action = "Delete Unused";
            spec.actionKey = "X";
            spec.actionKeys = KEY_X;
        }
        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key == KEY_X && !unusedList.empty()) {
            deleteUnusedTickets();
            continue;
        }
        if (result.key != KEY_A || result.index < 0 ||
            result.index >= static_cast<int>(shown.size())) {
            return;
        }
        const uint64_t ticket = shown[result.index];
        const std::string name = titles::displayName(ticket, index);
        const bool isUnused = unused.count(ticket) != 0;
        std::vector<ui::Paragraph> body;
        body.push_back({isUnused ? "Its software is not installed."
                                 : name + " will not start until it is "
                                          "installed again with its ticket.",
                        isUnused ? ui::color::Text : ui::color::Warning, 0.5f,
                        3});
        if (!confirmDanger("Delete", "Delete the ticket for " + name + "?",
                           body, "Delete")) {
            continue;
        }
        std::string error;
        if (!library::deleteTicket(ticket, error)) {
            ui::message("Not Deleted", error, ui::Tone::Error);
        }
        reloadLibrary();
    }
}

void App::deleteUnusedTickets()
{
    const std::vector<uint64_t> unused =
        titles::unusedTickets(library_.tickets, library_.titles);
    if (unused.empty()) {
        ui::message("Nothing to Delete", "Every ticket belongs to installed "
                                         "software.");
        return;
    }
    if (!confirmDanger("Delete",
                       "Delete " + plural(unused.size(), "unused ticket") + "?",
                       {{"They belong to software that is no longer installed. "
                         "Installing a CIA again brings its ticket back.",
                         ui::color::Text, 0.5f, 3}},
                       "Delete")) {
        return;
    }
    size_t deleted = 0;
    std::string error;
    tasks::busy("Delete", "Deleting unused tickets...", [&]() {
        for (const uint64_t ticket : unused) {
            if (library::deleteTicket(ticket, error)) {
                ++deleted;
            }
        }
    });
    if (deleted == unused.size()) {
        ui::message("Tickets Deleted", plural(deleted, "unused ticket") +
                                           " deleted.",
                    ui::Tone::Success);
    } else {
        ui::message("Some Tickets Remain",
                    plural(deleted, "ticket") + " of " +
                        std::to_string(unused.size()) + " deleted.",
                    ui::Tone::Warning, error);
    }
    reloadLibrary();
}

void App::cleanUpScreen()
{
    int focus = 0;
    while (ui::keepRunning() && !quit_) {
        const size_t pending = library_.pending.size();
        const size_t unused =
            titles::unusedTickets(library_.tickets, library_.titles).size();
        ui::ScreenSpec spec;
        spec.back = "Back";
        const auto add = [&](ui::Rect rect, const std::string &label,
                             bool enabled) {
            ui::Control control;
            control.rect = rect;
            control.label = label;
            control.icon = ui::Icon::Trash;
            control.iconColor = ui::color::IconRed;
            control.enabled = enabled;
            control.scale = ui::type::Small + 0.06f;
            spec.controls.push_back(control);
        };
        add({22, 30, 276, 44}, "Remove Unfinished Installs", pending > 0);
        add({22, 86, 276, 44}, "Delete Unused Tickets", unused > 0);
        spec.drawTop = [&]() {
            ui::drawTopChrome("Clean Up", ui::Icon::Trash, ui::color::IconRed);
            ui::drawPanel();
            const float left = ui::TopPanel.x + 18;
            float y = ui::drawWrapped(
                          "An install that stopped halfway leaves an "
                          "unfinished entry behind, and the same software "
                          "can then refuse to install again.",
                          left, ui::TopPanel.y + 12, ui::type::Small,
                          ui::color::Ink, ui::TopPanel.w - 36, 4) +
                      10;
            y = ui::drawTagRow(left, y, 110, "Unfinished",
                               plural(pending, "install"),
                               pending > 0 ? ui::color::Badge : ui::color::Tag);
            ui::drawTagRow(left, y, 110, "Unused", plural(unused, "ticket"));
        };
        spec.drawBottom = [&]() {
            ui::drawWrapped(pending + unused == 0
                                ? "Nothing needs cleaning up."
                                : "Installed software and its data are not "
                                  "touched.",
                            160, 150, ui::type::Tiny, ui::color::InkSoft, 270,
                            2, C2D_AlignCenter);
        };
        const ui::ScreenResult result = ui::runScreen(spec, focus);
        if (result.choice == 0) {
            if (!confirmDanger("Clean Up",
                               "Remove " + plural(pending, "unfinished install") +
                                   "?",
                               {{"What these installs left behind is removed "
                                 "so the software can be installed again.",
                                 ui::color::Text, 0.5f, 3}},
                               "Remove")) {
                continue;
            }
            size_t removed = 0;
            std::string error;
            tasks::busy("Clean Up", "Removing unfinished installs...", [&]() {
                for (const library::PendingTitle &title : library_.pending) {
                    if (library::deletePending(title, error)) {
                        ++removed;
                    }
                }
            });
            ui::message(removed == pending ? "Cleaned Up" : "Some Remain",
                        plural(removed, "unfinished install") + " removed.",
                        removed == pending ? ui::Tone::Success
                                           : ui::Tone::Warning,
                        error);
            reloadLibrary();
        } else if (result.choice == 1) {
            deleteUnusedTickets();
        } else {
            return;
        }
    }
}

void App::importSeeds()
{
    std::string path;
    for (const char *place : SeedDbPlaces) {
        if (fs::fileExists(place)) {
            path = place;
            break;
        }
    }
    if (path.empty()) {
        ui::message("No seeddb.bin Found",
                    "Put a seeddb.bin in sdmc:/, sdmc:/3ds/ or "
                    "sdmc:/3ds/3dsDL/ and try again. A single game's seed can "
                    "also be fetched from Nintendo with Get Seed in Software.",
                    ui::Tone::Info);
        return;
    }
    std::string data;
    std::vector<titles::SeedEntry> entries;
    std::string error;
    if (!fs::readFile(path, data) || !titles::parseSeedDb(data, entries, error)) {
        ui::message("Seeds Not Imported",
                    error.empty() ? "Could not read " + path + "." : error,
                    ui::Tone::Error, path);
        return;
    }
    const std::vector<titles::SeedEntry> wanted =
        titles::seedsFor(entries, library_.titles);
    if (wanted.empty()) {
        ui::message("No Seeds Needed",
                    "None of the games installed here are in " + path + " (" +
                        plural(entries.size(), "seed") + ").");
        return;
    }
    ui::Dialog dialog;
    dialog.header = "Seeds";
    dialog.title = "Add seeds for " + plural(wanted.size(), "installed game") +
                   "?";
    dialog.tone = ui::Tone::Info;
    dialog.body.push_back({"From " + path + ", which holds " +
                               plural(entries.size(), "seed") +
                               ". Only the installed games' seeds are added.",
                           ui::color::Text, 0.5f, 3});
    dialog.buttons = {"Add", "Cancel"};
    dialog.cancelButton = 1;
    if (ui::choose(dialog) != 0) {
        return;
    }
    size_t added = 0;
    tasks::busy("Seeds", "Adding seeds...", [&]() {
        for (const titles::SeedEntry &entry : wanted) {
            if (installer::addSeed(entry.titleId, entry.seed.data(), error)) {
                ++added;
            }
        }
    });
    ui::message(added == wanted.size() ? "Seeds Added" : "Some Seeds Not Added",
                plural(added, "seed") + " added.",
                added == wanted.size() ? ui::Tone::Success : ui::Tone::Warning,
                error);
}
