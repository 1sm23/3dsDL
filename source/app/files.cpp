#include "app.hpp"

#include "app_util.hpp"
#include "cia.hpp"
#include "file_ops.hpp"
#include "file_types.hpp"
#include "fs_util.hpp"
#include "installer.hpp"
#include "library.hpp"
#include "naming.hpp"
#include "picture.hpp"
#include "platform.hpp"
#include "sha256.hpp"
#include "storage.hpp"
#include "tasks.hpp"
#include "text.hpp"
#include "text_doc.hpp"
#include "title_meta.hpp"
#include "ui.hpp"
#include "units.hpp"
#include "update.hpp"
#include "zip.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace {

using filetype::Kind;
using screens::basename;
using screens::ciaName;
using screens::drawLabelledPanel;
using screens::plural;
using screens::versionRow;

// The background behind pictures, as RGB565: the app's dark blue.
constexpr uint16_t PictureBackground = 0x0883;

std::string folderOf(const std::string &path)
{
    return path.substr(0, path.find_last_of('/') + 1);
}

Kind kindOf(const fs::Entry &entry)
{
    return entry.folder ? Kind::Folder : filetype::fromName(entry.name);
}

const char *sortLabel(fs::SortOrder order)
{
    switch (order) {
    case fs::SortOrder::Size:
        return "Size";
    case fs::SortOrder::Date:
        return "Date";
    case fs::SortOrder::Type:
        return "Type";
    case fs::SortOrder::Name:
        break;
    }
    return "Name";
}

std::string sizeText(int64_t size)
{
    return size >= 0 ? units::bytes(static_cast<uint64_t>(size)) : "";
}

bool confirm(const std::string &header, const std::string &title,
             const std::string &body, const std::string &yes,
             bool dangerous)
{
    ui::Dialog dialog;
    dialog.header = header;
    dialog.title = title;
    dialog.tone = dangerous ? ui::Tone::Warning : ui::Tone::Info;
    dialog.body.push_back({body, ui::color::Text, 0.48f, 5});
    dialog.buttons = {yes, "Cancel"};
    dialog.dangerous = dangerous ? 0 : -1;
    dialog.initial = dangerous ? 1 : 0;
    dialog.cancelButton = 1;
    return ui::choose(dialog) == 0;
}

std::string totalsText(const fileops::Totals &totals)
{
    std::string text = plural(totals.files, "file");
    if (totals.folders > 0) {
        text += " in " + plural(totals.folders, "folder");
    }
    return text + ", " + units::bytes(totals.bytes);
}

// What the top screen shows about an entry, worked out once because it
// reads the SD card.
struct Details {
    bool ciaReadable = false;
    cia::Info cia;
    bool installed = false;
    uint16_t installedVersion = 0;
    bool metaReadable = false;
    titlemeta::Meta meta;
    std::string error;
    std::vector<std::string> head;
    uint32_t width = 0;
    uint32_t height = 0;
    bool counted = false;
    size_t items = 0;
    int64_t modified = 0; // for folders listed without times
};

Details loadDetails(const std::string &path, Kind kind)
{
    Details details;
    details.modified = fs::modifiedTime(path);
    switch (kind) {
    case Kind::Folder:
        details.items =
            fs::listEntries(path + "/", nullptr, true, false).size();
        details.counted = true;
        break;
    case Kind::Cia:
        details.ciaReadable =
            cia::readInfo(path, details.cia, details.error);
        if (details.ciaReadable) {
            details.installed = installer::installedVersion(
                details.cia.titleId, details.installedVersion);
        }
        break;
    case Kind::Image:
        picture::size(path, details.width, details.height);
        break;
    case Kind::Text:
    case Kind::Other:
        details.head = textdoc::head(path, 9, 2048);
        break;
    default:
        if (filetype::hasTitleMeta(kind)) {
            details.metaReadable =
                titlemeta::read(path, kind, details.meta, details.error);
        }
        break;
    }
    return details;
}

struct KindLook {
    ui::Icon icon;
    u32 color;
};

KindLook lookOf(Kind kind)
{
    switch (kind) {
    case Kind::Folder:
        return {ui::Icon::Folder, ui::color::IconOrange};
    case Kind::Cia:
        return {ui::Icon::Package, ui::color::IconBlue};
    case Kind::Ticket:
        return {ui::Icon::Lock, ui::color::IconGrey};
    case Kind::Homebrew:
        return {ui::Icon::App, ui::color::IconTeal};
    case Kind::Smdh:
        return {ui::Icon::Picture, ui::color::IconTeal};
    case Kind::CartridgeImage:
    case Kind::Nds:
        return {ui::Icon::GameCard, ui::color::IconRed};
    case Kind::Firm:
        return {ui::Icon::Chip, ui::color::IconGrey};
    case Kind::Image:
        return {ui::Icon::Picture, ui::color::IconPink};
    case Kind::Text:
        return {ui::Icon::Text, ui::color::IconGrey};
    case Kind::Zip:
        return {ui::Icon::Zip, ui::color::IconGold};
    case Kind::Audio:
        return {ui::Icon::Music, ui::color::IconPink};
    case Kind::Video:
        return {ui::Icon::Video, ui::color::IconPink};
    case Kind::Other:
        break;
    }
    return {ui::Icon::File, ui::color::IconGrey};
}

// The Files top screen: the title row, then a panel about the highlighted
// entry with its icon, name, a second line and label tags.
void drawEntryPanel(const std::vector<uint16_t> &titleIcon, Kind kind,
                    const std::string &name, const std::string &second,
                    const std::vector<std::pair<std::string, std::string>> &tags)
{
    ui::drawPanel();
    const float left = ui::TopPanel.x + 18;
    const float top = ui::TopPanel.y + 12;
    if (!titleIcon.empty()) {
        ui::drawTitleIcon(titleIcon, left, top);
    } else {
        const KindLook look = lookOf(kind);
        ui::fillRound({left - 1, top - 1, 50, 50}, 9, ui::color::rgb(0xC9C08A));
        ui::fillRound({left, top, 48, 48}, 8,
                      {{0, ui::color::rgb(0xFEFCF5)}, {1, ui::color::rgb(0xE6E2D0)}});
        ui::drawIcon(look.icon, left + 9, top + 9, 30, look.color);
    }
    ui::drawMarquee(name, left + 60, top + 2, 0.62f, ui::color::InkStrong, 290);
    ui::drawMarquee(second, left + 60, top + 28, ui::type::Tiny,
                    ui::color::InkSoft, 290);
    float y = top + 58;
    for (const auto &tag : tags) {
        if (tag.second.empty() || y > ui::TopPanel.y + ui::TopPanel.h - 24) {
            continue;
        }
        y = ui::drawTagRow(left, y, 82, tag.first, tag.second);
    }
}

void drawEntryTop(const std::string &path, const fs::Entry &entry,
                  const Details &details)
{
    const Kind kind = kindOf(entry);
    const int64_t time = entry.modified > 0 ? entry.modified : details.modified;
    const std::string modified = time > 0 ? units::dateTime(time) : "";

    if (kind == Kind::Folder) {
        drawEntryPanel({}, kind, entry.name, path + "/",
                       {{"Type", "Folder"},
                        {"Contains", plural(details.items, "item")},
                        {"Changed", modified}});
        return;
    }
    if (kind == Kind::Cia && details.ciaReadable) {
        const cia::Info &info = details.cia;
        drawEntryPanel(info.smdh.icon, kind, ciaName(info),
                       info.smdh.publisher.empty() ? entry.name
                                                   : info.smdh.publisher,
                       {{"Type", std::string("CIA, ") +
                                     cia::kindName(cia::kind(info.titleId))},
                        {"Version",
                         versionRow(info.titleId, info.version,
                                    details.installed,
                                    details.installedVersion)},
                        {"Size", units::bytes(info.fileSize)}});
        return;
    }
    if (details.metaReadable) {
        const titlemeta::Meta &meta = details.meta;
        drawEntryPanel(meta.icon, kind,
                       meta.name.empty() ? entry.name : meta.name,
                       meta.publisher.empty() ? filetype::label(kind)
                                              : meta.publisher,
                       {{"Type", filetype::label(kind)},
                        {"Title ID", meta.hasTitleId
                                         ? cia::titleIdText(meta.titleId)
                                         : ""},
                        {"Code", meta.productCode},
                        {"Note", meta.note},
                        {"Size", sizeText(entry.size)}});
        return;
    }
    if (!details.head.empty()) {
        // The start of a text file, as on the canvas.
        drawEntryPanel({}, kind, entry.name,
                       std::string(filetype::label(kind)) + ", " +
                           sizeText(entry.size),
                       {});
        float y = ui::TopPanel.y + 70;
        for (const std::string &line : details.head) {
            if (y > ui::TopPanel.y + ui::TopPanel.h - 16) {
                break;
            }
            ui::drawText(ui::fitEnd(line, ui::type::Tiny, 330),
                         ui::TopPanel.x + 18, y, ui::type::Tiny,
                         ui::color::InkSoft);
            y += 13;
        }
        return;
    }
    drawEntryPanel({}, kind, entry.name, filetype::label(kind),
                   {{"Picture", details.width > 0
                                    ? std::to_string(details.width) + " x " +
                                          std::to_string(details.height) +
                                          " pixels"
                                    : ""},
                    {"Size", sizeText(entry.size)},
                    {"Changed", modified},
                    {"Problem", details.error}});
}

// --- Text editor helpers -------------------------------------------------------

u32 lineColor(const std::string &line, const std::string &extension)
{
    const std::string trimmed = text::trim(line);
    if (trimmed.empty()) {
        return ui::color::Text;
    }
    if (extension == ".md" || extension == ".markdown") {
        return trimmed[0] == '#' ? ui::color::Primary : ui::color::Text;
    }
    if (trimmed[0] == '#' || trimmed[0] == ';' ||
        trimmed.compare(0, 2, "//") == 0) {
        return ui::color::Muted;
    }
    if (trimmed[0] == '[' && trimmed.back() == ']') {
        return ui::color::Primary;
    }
    return ui::color::Text;
}

std::vector<std::string> splitLines(const std::string &value)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        const size_t end = value.find('\n', start);
        std::string line = value.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
        if (end == std::string::npos) {
            return lines;
        }
        start = end + 1;
    }
}

} // namespace

// --- Browsing -------------------------------------------------------------------

void App::filesScreen()
{
    if (filesFolder_.empty() || !fs::directoryExists(filesFolder_)) {
        filesFolder_ = fs::directoryExists(settings_.destination)
            ? naming::normaliseFolder(settings_.destination)
            : "sdmc:/";
    }
    std::string &current = filesFolder_;
    int cursor = 0;
    std::string landOn; // name to put the cursor on after reloading
    std::vector<std::string> marked;
    std::map<std::string, Details> details;

    // quit_ is set when a .3dsx is started, which happens as 3dsDL closes.
    while (ui::keepRunning() && !quit_) {
        bool readable = false;
        // Times cost the SD card a search of the folder for each entry, so
        // they are only read to sort by them; the details read their own.
        std::vector<fs::Entry> entries =
            fs::listEntries(current, &readable, settings_.showHiddenFiles,
                            settings_.fileSort == fs::SortOrder::Date);
        if (!readable && current != "sdmc:/") {
            current = "sdmc:/";
            cursor = 0;
            marked.clear();
            continue;
        }
        fs::sortEntries(entries, settings_.fileSort);
        const bool atRoot = naming::parentFolder(current) == current;
        // The Back tab and B go up a folder, so there is no ".." row.
        const int first = 0;

        // Marks for entries that are gone are dropped.
        marked.erase(std::remove_if(marked.begin(), marked.end(),
                                    [](const std::string &path) {
                                        return !fileops::exists(path);
                                    }),
                     marked.end());
        const std::set<std::string> markedSet(marked.begin(), marked.end());

        if (!landOn.empty()) {
            for (size_t index = 0; index < entries.size(); ++index) {
                if (entries[index].name == landOn) {
                    cursor = static_cast<int>(index) + first;
                }
            }
            landOn.clear();
        }

        ui::ListSpec spec;
        spec.title = current;
        spec.emptyText = readable
            ? "This folder is empty. Options makes a new file or folder here."
            : "The SD card could not be read.";
        spec.back = atRoot ? "Close" : "Back";
        spec.action = "Options";
        spec.actionKey = "X";
        spec.actionKeys = KEY_X;
        spec.longPressKey = KEY_X;
        spec.extraKeys = KEY_Y | KEY_SELECT | KEY_START;
        spec.hint = !marked.empty() ? std::to_string(marked.size()) + " marked"
                  : !clipboard_.empty()
                      ? std::to_string(clipboard_.size()) +
                            (clipboardCut_ ? " to move" : " to paste")
                      : "";
        for (const fs::Entry &entry : entries) {
            ui::ListItem item;
            const KindLook look = lookOf(kindOf(entry));
            item.label = entry.name;
            item.detail = entry.folder ? "" : sizeText(entry.size);
            item.icon = look.icon;
            item.iconColor = look.color;
            item.labelColor = entry.name[0] == '.' ? ui::color::InkSoft
                                                   : ui::color::InkButton;
            item.marked = markedSet.count(current + entry.name) > 0;
            if (item.marked) {
                item.badge = "Marked";
                item.badgeColor = ui::color::Tag;
            }
            spec.items.push_back(item);
        }

        spec.drawTop = [&](int index) {
            const std::string title =
                atRoot ? "SD Card" : fileops::name(current);
            ui::drawTopChrome(title, ui::Icon::Folder, ui::color::IconOrange);
            const int position = index - first;
            if (position < 0 ||
                position >= static_cast<int>(entries.size())) {
                ui::drawPanel();
                std::string summary = plural(entries.size(), "item") +
                                      ", sorted by " +
                                      text::lower(sortLabel(settings_.fileSort)) +
                                      ".";
                if (!clipboard_.empty()) {
                    summary += " " + plural(clipboard_.size(), "item") +
                               " ready to paste from Options.";
                }
                ui::drawPanelText(summary, ui::TopPanel);
                return;
            }
            const fs::Entry &entry = entries[position];
            const std::string path = current + entry.name;
            auto found = details.find(path);
            if (found == details.end()) {
                found = details.emplace(path, loadDetails(path, kindOf(entry)))
                            .first;
            }
            drawEntryTop(path, entry, found->second);
        };

        const ui::ListResult result = ui::runList(spec, cursor);
        const int position = result.index - first;
        const bool onEntry =
            position >= 0 && position < static_cast<int>(entries.size());
        const std::string highlighted =
            onEntry ? current + entries[position].name : "";

        if (result.key == 0 || result.key == KEY_START) {
            return;
        }
        if (result.key == KEY_B) {
            if (atRoot) {
                return;
            }
            landOn = fileops::name(current);
            current = naming::parentFolder(current);
            cursor = 0;
            marked.clear();
            continue;
        }
        if (result.key == KEY_Y) {
            if (onEntry) {
                const auto at =
                    std::find(marked.begin(), marked.end(), highlighted);
                if (at == marked.end()) {
                    marked.push_back(highlighted);
                } else {
                    marked.erase(at);
                }
                cursor = std::min(cursor + 1,
                                  static_cast<int>(spec.items.size()) - 1);
            }
            continue;
        }
        if (result.key == KEY_SELECT || (result.key == KEY_X && !onEntry &&
                                         marked.empty())) {
            std::vector<std::string> all;
            for (const fs::Entry &entry : entries) {
                all.push_back(current + entry.name);
            }
            if (folderOptions(current, all, marked)) {
                details.clear();
            }
            continue;
        }
        if (result.key == KEY_X) {
            std::vector<std::string> all;
            for (const fs::Entry &entry : entries) {
                all.push_back(current + entry.name);
            }
            const std::vector<std::string> targets =
                marked.empty() ? std::vector<std::string>{highlighted} : marked;
            if (entryOptions(current, targets, marked, all)) {
                details.clear();
                if (!marked.empty() &&
                    std::none_of(marked.begin(), marked.end(),
                                 [](const std::string &path) {
                                     return fileops::exists(path);
                                 })) {
                    marked.clear();
                }
            }
            continue;
        }
        if (result.key != KEY_A || !onEntry) {
            continue;
        }
        const fs::Entry &entry = entries[position];
        if (entry.folder) {
            current += entry.name + "/";
            cursor = 0;
            marked.clear();
            continue;
        }
        openFile(highlighted);
        details.erase(highlighted);
        landOn = entry.name;
    }
}

void App::openFile(const std::string &path)
{
    const Kind kind = filetype::fromName(basename(path));
    switch (kind) {
    case Kind::Cia:
        ciaOptions(path);
        return;
    case Kind::Text:
        textEditor(path);
        return;
    case Kind::Zip:
        zipViewer(path);
        return;
    case Kind::Image: {
        const std::string folder = folderOf(path);
        std::vector<std::string> pictures;
        size_t index = 0;
        std::vector<fs::Entry> entries =
            fs::listEntries(folder, nullptr, settings_.showHiddenFiles,
                            settings_.fileSort == fs::SortOrder::Date);
        fs::sortEntries(entries, settings_.fileSort);
        for (const fs::Entry &entry : entries) {
            if (!entry.folder && kindOf(entry) == Kind::Image) {
                if (folder + entry.name == path) {
                    index = pictures.size();
                }
                pictures.push_back(folder + entry.name);
            }
        }
        pictureViewer(pictures, index);
        return;
    }
    default:
        break;
    }
    if (filetype::hasTitleMeta(kind)) {
        titleDetails(path, kind);
        return;
    }
    // Anything else opens as text when it looks like text.
    if (!textdoc::head(path, 1, 4096).empty() || fs::fileSize(path) == 0) {
        textEditor(path);
    } else {
        hexViewer(path);
    }
}

void App::ciaOptions(const std::string &path)
{
    cia::Info info;
    std::string error;
    if (!cia::readInfo(path, info, error)) {
        ui::Dialog dialog;
        dialog.header = "CIA Installer";
        dialog.title = "This CIA cannot be installed.";
        dialog.tone = ui::Tone::Warning;
        dialog.body.push_back({basename(path), ui::color::Text, 0.5f, 2});
        dialog.body.push_back({error, ui::color::Warning, 0.48f, 3});
        dialog.buttons = {"Open as Hex", "Close"};
        dialog.initial = 1;
        dialog.cancelButton = 1;
        if (ui::choose(dialog) == 0) {
            hexViewer(path);
        }
        return;
    }

    ui::ScreenSpec spec;
    spec.back = "Back";
    const auto add = [&](ui::Rect rect, const char *label, float scale,
                         u32 labelColor) {
        ui::Control control;
        control.rect = rect;
        control.label = label;
        control.scale = scale;
        control.labelColor = labelColor;
        control.enabled = installer::available() || labelColor != 0;
        spec.controls.push_back(control);
    };
    add({26, 42, 268, 38}, "Install", ui::type::Button, 0);
    add({26, 90, 268, 38}, "Install and Delete File", ui::type::Body + 0.06f, 0);
    add({26, 140, 268, 26}, "Delete File", ui::type::Small + 0.06f,
        ui::color::Bad);
    const std::string state = installer::available()
                                  ? screens::installState(info)
                                  : "Installing is not available.";
    spec.drawTop = [&]() { screens::drawCiaTop(info, "CIA Installer"); };
    spec.drawBottom = [&]() {
        ui::drawMarquee(state, 160, 12, ui::type::Body, ui::color::Ink, 300,
                        C2D_AlignCenter);
    };
    int focus = installer::available() ? 0 : 2;
    const ui::ScreenResult result = ui::runScreen(spec, focus);
    if (result.choice == 0 || result.choice == 1) {
        installCia(path, info, result.choice == 1, "");
    } else if (result.choice == 2) {
        deleteEntries({path});
    }
}

// --- Options --------------------------------------------------------------------

void App::showChecksum(const std::string &path)
{
    std::string hash;
    tasks::busy("SHA-256", "Reading " + basename(path) + "...",
                [&]() { hash = sha256::ofFile(path); });
    if (hash.empty()) {
        ui::message("No Checksum", "The file could not be read.",
                    ui::Tone::Error, path);
        return;
    }
    // A checksum list next to the file, as releases publish them.
    const std::string folder = folderOf(path);
    const std::string name = basename(path);
    std::string verdict;
    bool matches = false;
    for (const fs::Entry &entry : fs::listEntries(folder, nullptr, false, false)) {
        if (entry.folder || !text::endsWithNoCase(entry.name, ".sha256")) {
            continue;
        }
        std::string listing;
        if (fs::fileSize(folder + entry.name) > 1024 * 1024 ||
            !fs::readFile(folder + entry.name, listing)) {
            continue;
        }
        const std::string expected = update::checksumFor(listing, name);
        if (!expected.empty()) {
            matches = expected == hash;
            verdict = matches ? "It matches " + entry.name + "."
                              : "It does not match " + entry.name +
                                    ", which expects " + expected + ".";
            break;
        }
    }
    ui::Dialog dialog;
    dialog.header = "SHA-256";
    dialog.title = verdict.empty() ? "SHA-256 of " + name
                   : matches       ? name + " is intact."
                                   : name + " does not match its checksum.";
    dialog.tone = verdict.empty() ? ui::Tone::Info
                  : matches       ? ui::Tone::Success
                                  : ui::Tone::Error;
    dialog.body.push_back({name, ui::color::Text, 0.5f, 2});
    dialog.body.push_back({hash.substr(0, 32) + "\n" + hash.substr(32),
                           ui::color::Text, 0.46f, 2});
    if (!verdict.empty()) {
        dialog.body.push_back({verdict,
                               matches ? ui::color::Good : ui::color::Bad,
                               0.44f, 3});
    }
    dialog.buttons = {"Close"};
    ui::choose(dialog);
}

bool App::entryOptions(const std::string &folder,
                       const std::vector<std::string> &paths,
                       std::vector<std::string> &marked,
                       const std::vector<std::string> &all)
{
    if (paths.empty() || paths.front().empty()) {
        return folderOptions(folder, all, marked);
    }
    enum class Action {
        Open,
        Install,
        InstallDelete,
        InstallTicket,
        InstallAll,
        InstallDeleteAll,
        Extract,
        Edit,
        ViewText,
        ViewHex,
        Rename,
        Copy,
        Cut,
        Delete,
        Properties,
        Checksum,
        Mark,
        ClearMarks,
        Folder
    };
    std::vector<ui::MenuItem> items;
    std::vector<Action> actions;
    std::vector<std::string> notes;
    const auto add = [&](const std::string &label, Action action,
                         const std::string &note, bool danger = false) {
        items.push_back({label, danger, true});
        actions.push_back(action);
        notes.push_back(note);
    };

    const bool single = paths.size() == 1;
    const std::string &path = paths.front();
    const bool isFolder = single && fileops::isFolder(path);
    const Kind kind = isFolder ? Kind::Folder
                             : filetype::fromName(basename(path));
    std::vector<std::string> cias;
    for (const std::string &entry : paths) {
        if (!fileops::isFolder(entry) &&
            filetype::fromName(basename(entry)) == Kind::Cia) {
            cias.push_back(entry);
        }
    }
    if (isFolder) {
        for (const fs::Entry &entry :
             fs::listEntries(path + "/", nullptr, false, false)) {
            if (!entry.folder && kindOf(entry) == Kind::Cia) {
                cias.push_back(path + "/" + entry.name);
            }
        }
    }
    const bool isMarked =
        single && std::find(marked.begin(), marked.end(), path) != marked.end();

    if (single && kind == Kind::Cia) {
        add("Install", Action::Install, "Installs the software and keeps the CIA file.");
        add("Install and Delete", Action::InstallDelete,
            "Installs the software, then removes the CIA file from the SD card.");
    } else if (single && kind == Kind::Ticket) {
        add("Install Ticket", Action::InstallTicket,
            "Adds this ticket to the console, replacing one it already has "
            "for the same title, as FBI does.");
    } else if (single && kind == Kind::Text) {
        add("Edit", Action::Edit, "Opens the file in the line editor.");
    } else if (single) {
        add("Open", Action::Open, isFolder ? "Opens the folder."
                                           : "Opens the file in the viewer that suits it.");
        if (kind == Kind::Zip) {
            add("Extract", Action::Extract, "Extracts the archive here or into a new folder.");
        }
    }
    if ((!single || isFolder) && !cias.empty()) {
        add("Install " + plural(cias.size(), "CIA"), Action::InstallAll,
            "Installs each CIA one after another.");
        add("Install and Delete", Action::InstallDeleteAll,
            "Installs each CIA, then removes the files that installed.");
    }
    if (single && !isFolder && kind != Kind::Text && kind != Kind::Cia) {
        add("Open as Text", Action::ViewText, "Shows the file as text, even if it is not meant to be.");
    }
    if (single && !isFolder) {
        add("Open as Hex", Action::ViewHex, "Shows the file byte by byte.");
    }
    if (single) {
        add("Rename", Action::Rename, "Gives it a new name.");
    }
    add("Copy", Action::Copy, "Copies " + std::string(single ? "it" : "them") +
                                  ". Open another folder and choose Paste.");
    add("Cut", Action::Cut, "Moves " + std::string(single ? "it" : "them") +
                                ". Open another folder and choose Paste.");
    add("Delete", Action::Delete, "Deletes " + std::string(single ? "it" : "them") +
                                      " for good, after asking.", true);
    add("Properties", Action::Properties, "Size, date and where it is.");
    if (single && !isFolder) {
        add("SHA-256", Action::Checksum,
            "Works out the file's SHA-256 checksum and compares it with any "
            ".sha256 list in the same folder.");
    }
    if (single) {
        add(isMarked ? "Unmark" : "Mark", Action::Mark,
            "Marked items are copied, moved, installed or deleted together.");
    } else {
        add("Clear Marks", Action::ClearMarks, "Unmarks everything.");
    }
    add("This Folder...", Action::Folder,
        "Paste, new folder, new file, sorting and hidden files.");

    const std::string title = single ? basename(path)
                                     : plural(paths.size(), "Marked Item");
    const KindLook look = lookOf(kind);
    const int choice = ui::menu(items, [&](int focus) {
        ui::drawTopChrome(title, single ? look.icon : ui::Icon::Check,
                          single ? look.color : ui::color::Tag);
        ui::drawPanel();
        ui::drawPanelText(focus >= 0 && focus < static_cast<int>(notes.size())
                              ? notes[focus]
                              : "",
                          ui::TopPanel);
    });
    if (choice < 0) {
        return false;
    }

    switch (actions[choice]) {
    case Action::Open:
        if (isFolder) {
            filesFolder_ = path + "/";
            marked.clear();
        } else {
            openFile(path);
        }
        return true;
    case Action::Install:
    case Action::InstallDelete:
        installCias({path}, actions[choice] == Action::InstallDelete);
        return true;
    case Action::InstallTicket:
        installTicketFile(path);
        return false;
    case Action::InstallAll:
    case Action::InstallDeleteAll:
        installCias(cias, actions[choice] == Action::InstallDeleteAll);
        return true;
    case Action::Extract:
        offerExtraction(path, folderOf(path), "",
                        "Extract " + basename(path) + "?");
        return true;
    case Action::Edit:
    case Action::ViewText:
        textEditor(path);
        return true;
    case Action::ViewHex:
        hexViewer(path);
        return false;
    case Action::Rename:
        return renameEntry(path);
    case Action::Copy:
    case Action::Cut:
        clipboard_ = paths;
        clipboardCut_ = actions[choice] == Action::Cut;
        ui::message(clipboardCut_ ? "Ready to Move" : "Ready to Copy",
                    "Open the folder to put " +
                        std::string(single ? "it" : "them") +
                        " in, then choose Paste in Options.");
        return true;
    case Action::Delete:
        return deleteEntries(paths);
    case Action::Properties:
        showProperties(paths);
        return false;
    case Action::Checksum:
        showChecksum(path);
        return false;
    case Action::Mark:
        if (isMarked) {
            marked.erase(std::find(marked.begin(), marked.end(), path));
        } else {
            marked.push_back(path);
        }
        return false;
    case Action::ClearMarks:
        marked.clear();
        return false;
    case Action::Folder:
        return folderOptions(folder, all, marked);
    }
    return false;
}

bool App::folderOptions(const std::string &folder,
                        const std::vector<std::string> &entries,
                        std::vector<std::string> &marked)
{
    enum class Action {
        Paste,
        ClearClipboard,
        NewFolder,
        NewFile,
        Sort,
        Hidden,
        MarkAll,
        ClearMarks,
        InstallAll,
        Properties
    };
    std::vector<ui::MenuItem> items;
    std::vector<Action> actions;
    std::vector<std::string> notes;
    const auto add = [&](const std::string &label, Action action,
                         const std::string &note) {
        items.push_back({label, false, true});
        actions.push_back(action);
        notes.push_back(note);
    };

    if (!clipboard_.empty()) {
        add(clipboardCut_ ? "Move Here" : "Paste Here", Action::Paste,
            std::string(clipboardCut_ ? "Moves " : "Copies ") +
                plural(clipboard_.size(), "item") + " into this folder.");
        add("Forget Clipboard", Action::ClearClipboard,
            "Forgets what was copied or cut. Nothing is deleted.");
    }
    add("New Folder", Action::NewFolder, "Makes an empty folder here.");
    add("New File", Action::NewFile,
        "Makes an empty file here and opens it in the editor, for configs "
        "and notes.");
    add(std::string("Sort: ") + sortLabel(settings_.fileSort), Action::Sort,
        "Sorts by name, size, date or type. Folders always come first.");
    add(std::string("Hidden: ") + (settings_.showHiddenFiles ? "On" : "Off"),
        Action::Hidden, "Shows or hides files and folders whose names start "
                        "with a dot.");
    if (!entries.empty()) {
        add("Mark All", Action::MarkAll, "Marks everything in this folder.");
    }
    if (!marked.empty()) {
        add("Clear Marks", Action::ClearMarks, "Unmarks everything.");
    }
    size_t cias = 0;
    for (const std::string &entry : entries) {
        if (!fileops::isFolder(entry) &&
            filetype::fromName(basename(entry)) == Kind::Cia) {
            ++cias;
        }
    }
    if (cias > 0) {
        add("Install All CIAs", Action::InstallAll,
            "Installs " + plural(cias, "CIA") + " in this folder, one after "
            "another.");
    }
    add("Properties", Action::Properties, "How much this folder holds.");

    const std::string name =
        naming::parentFolder(folder) == folder ? "SD Card" : fileops::name(folder);
    const int choice = ui::menu(items, [&](int focus) {
        ui::drawTopChrome(name, ui::Icon::Folder, ui::color::IconOrange);
        ui::drawPanel();
        ui::drawPanelText(focus >= 0 && focus < static_cast<int>(notes.size())
                              ? notes[focus]
                              : folder,
                          ui::TopPanel);
    });
    if (choice < 0) {
        return false;
    }
    switch (actions[choice]) {
    case Action::Paste:
        return pasteInto(folder);
    case Action::ClearClipboard:
        clipboard_.clear();
        return false;
    case Action::NewFolder:
        return createFolder(folder);
    case Action::NewFile:
        return createFile(folder);
    case Action::Sort: {
        const int picked = ui::chooseOption(
            "Sort Files", "Folders always come first, sorted by name.",
            {"Name", "Size, Largest First", "Date, Newest First", "Type"},
            static_cast<int>(settings_.fileSort), ui::Icon::Folder,
            ui::color::IconOrange);
        if (picked < 0) {
            return false;
        }
        settings_.fileSort = static_cast<fs::SortOrder>(picked);
        persistSettings();
        return true;
    }
    case Action::Hidden:
        settings_.showHiddenFiles = !settings_.showHiddenFiles;
        persistSettings();
        return true;
    case Action::MarkAll:
        marked = entries;
        return false;
    case Action::ClearMarks:
        marked.clear();
        return false;
    case Action::InstallAll: {
        std::vector<std::string> paths;
        for (const std::string &entry : entries) {
            if (!fileops::isFolder(entry) &&
                filetype::fromName(basename(entry)) == Kind::Cia) {
                paths.push_back(entry);
            }
        }
        ui::Dialog dialog;
        dialog.header = "install";
        dialog.title = "Install " + plural(paths.size(), "CIA") + "?";
        dialog.body.push_back({"From " + folder, ui::color::Muted, 0.46f, 2});
        dialog.buttons = {"Install", "Install and Delete", "Cancel"};
        dialog.cancelButton = 2;
        const int answer = ui::choose(dialog);
        if (answer == 0 || answer == 1) {
            installCias(paths, answer == 1);
            return true;
        }
        return false;
    }
    case Action::Properties: {
        std::string path = folder;
        if (path.size() > 6 && path.back() == '/') {
            path.pop_back();
        }
        showProperties({path});
        return false;
    }
    }
    return false;
}

// --- File operations -------------------------------------------------------------

void App::installCias(const std::vector<std::string> &paths, bool deleteAfter)
{
    if (paths.size() == 1) {
        cia::Info info;
        std::string error;
        if (!cia::readInfo(paths.front(), info, error)) {
            ui::message("Cannot install", error, ui::Tone::Error,
                        basename(paths.front()));
            return;
        }
        installCia(paths.front(), info, deleteAfter, "");
        return;
    }

    size_t installed = 0;
    std::vector<std::string> failed;
    for (const std::string &path : paths) {
        if (!ui::keepRunning()) {
            return;
        }
        cia::Info info;
        std::string error;
        if (cia::readInfo(path, info, error) &&
            installCia(path, info, deleteAfter, "", true)) {
            ++installed;
        } else {
            failed.push_back(basename(path));
        }
    }
    std::string list;
    for (const std::string &name : failed) {
        list += (list.empty() ? "" : ", ") + name;
    }
    ui::message(failed.empty() ? "All installed" : "Some were not installed",
                "Installed " + std::to_string(installed) + " of " +
                    plural(paths.size(), "CIA") + ".",
                failed.empty() ? ui::Tone::Success : ui::Tone::Warning,
                failed.empty() ? "" : "Not installed: " + list);
}

bool App::renameEntry(const std::string &path)
{
    const std::string oldName = fileops::name(path);
    std::string typed;
    if (!platform::keyboard(platform::KeyboardKind::Name, "New name", oldName,
                            typed)) {
        return false;
    }
    const std::string newName = text::trim(typed);
    std::string error;
    if (newName == oldName) {
        return false;
    }
    if (!fileops::validName(newName, error)) {
        ui::message("Cannot use that name", error, ui::Tone::Error, newName);
        return false;
    }
    const std::string folder = folderOf(path);
    const std::string target = folder + newName;
    // FAT ignores case, so "a.txt" to "A.txt" goes through a spare name.
    if (text::lower(newName) == text::lower(oldName)) {
        const std::string spare = folder + ".3dsdl-rename-" + oldName;
        if (!fileops::move(path, spare, error) ||
            !fileops::move(spare, target, error)) {
            ui::message("Could not rename", error, ui::Tone::Error);
            return true;
        }
        return true;
    }
    if (!fileops::move(path, target, error)) {
        ui::message("Could not rename", error, ui::Tone::Error);
        return false;
    }
    return true;
}

bool App::deleteEntries(const std::vector<std::string> &paths)
{
    fileops::Totals totals;
    tasks::busy("delete", "Counting what will be deleted...", [&]() {
        for (const std::string &path : paths) {
            const fileops::Totals part = fileops::measure(path);
            totals.files += part.files;
            totals.folders += part.folders + (fileops::isFolder(path) ? 1 : 0);
            totals.bytes += part.bytes;
        }
    });
    const bool single = paths.size() == 1;
    const std::string what = single ? fileops::name(paths.front())
                                    : plural(paths.size(), "item");
    std::string body = "This cannot be undone.";
    if (totals.folders > 0 || !single) {
        body = "That is " + totalsText(totals) + ". " + body;
    }
    if (!confirm("delete", "Delete " + what + "?", body, "Delete", true)) {
        return false;
    }

    const fileops::Result result = tasks::fileOperation(
        "deleting", what, folderOf(paths.front()), totals.files, true,
        [&](fileops::Observer &observer) {
            fileops::Result all;
            uint64_t done = 0;
            for (const std::string &path : paths) {
                const fileops::Result part =
                    fileops::remove(path, observer, done, totals.files);
                all.files += part.files;
                if (part.status != fileops::Status::Ok) {
                    all.status = part.status;
                    all.message = part.message;
                    break;
                }
            }
            return all;
        },
        settings_.keepAwake);

    // Copied entries that no longer exist cannot be pasted.
    clipboard_.erase(std::remove_if(clipboard_.begin(), clipboard_.end(),
                                    [](const std::string &path) {
                                        return !fileops::exists(path);
                                    }),
                     clipboard_.end());
    if (result.status == fileops::Status::Failed) {
        ui::message("Delete stopped", result.message, ui::Tone::Error,
                    plural(result.files, "file") + " deleted before that.");
    } else if (result.status == fileops::Status::Cancelled &&
               ui::keepRunning()) {
        ui::message("Delete stopped",
                    plural(result.files, "file") + " deleted before you "
                                                   "stopped it.",
                    ui::Tone::Warning);
    }
    return true;
}

bool App::pasteInto(const std::string &folder)
{
    struct Step {
        std::string source;
        std::string destination;
        bool replace = false;
    };
    std::vector<Step> steps;
    std::vector<std::string> problems;

    for (const std::string &source : clipboard_) {
        if (!fileops::exists(source)) {
            problems.push_back(fileops::name(source) + " no longer exists.");
            continue;
        }
        const std::string name = fileops::name(source);
        Step step {source, folder + name, false};
        if (fileops::isFolder(source) &&
            fileops::isInside(step.destination, source)) {
            problems.push_back(name + " cannot go inside itself.");
            continue;
        }
        if (clipboardCut_ && step.destination == source) {
            continue; // already here
        }
        if (fileops::exists(step.destination)) {
            if (step.destination == source) {
                // Copying into its own folder makes "name (1)".
                step.destination =
                    naming::unusedPath(folder, name, fileops::exists);
            } else {
                ui::Dialog dialog;
                dialog.header = "paste";
                dialog.title = name + " is already here.";
                dialog.tone = ui::Tone::Warning;
                dialog.body.push_back({"Keep Both adds a number to the new "
                                       "one's name. Replace deletes the one "
                                       "already in " + folder + " first.",
                                       ui::color::Text, 0.46f, 4});
                dialog.buttons = {"Keep Both", "Replace", "Skip"};
                dialog.dangerous = 1;
                dialog.cancelButton = 2;
                const int answer = ui::choose(dialog);
                if (answer == 0) {
                    step.destination =
                        naming::unusedPath(folder, name, fileops::exists);
                } else if (answer == 1) {
                    step.replace = true;
                } else {
                    continue;
                }
            }
        }
        if (step.destination.empty()) {
            problems.push_back("No free name was found for " + name + ".");
            continue;
        }
        steps.push_back(step);
    }

    uint64_t bytes = 0;
    if (!clipboardCut_ && !steps.empty()) {
        tasks::busy("paste", "Measuring what will be copied...", [&]() {
            for (const Step &step : steps) {
                bytes += fileops::measure(step.source).bytes;
            }
        });
        const int64_t free = platform::sdFreeBytes();
        if (free >= 0 && static_cast<uint64_t>(free) < bytes) {
            ui::message("Not enough space",
                        "Copying needs " + units::bytes(bytes) + " and " +
                            units::bytes(static_cast<uint64_t>(free)) +
                            " is free.",
                        ui::Tone::Error);
            return false;
        }
    }

    fileops::Result result;
    if (!steps.empty()) {
        const bool cut = clipboardCut_;
        result = tasks::fileOperation(
            cut ? "moving" : "copying", plural(steps.size(), "item"), folder,
            bytes, false,
            [&](fileops::Observer &observer) {
                fileops::Result all;
                uint64_t done = 0;
                for (const Step &step : steps) {
                    if (observer.cancelRequested()) {
                        all.status = fileops::Status::Cancelled;
                        break;
                    }
                    if (step.replace) {
                        uint64_t removed = 0;
                        const fileops::Result cleared = fileops::remove(
                            step.destination, observer, removed, 0);
                        if (cleared.status != fileops::Status::Ok) {
                            all = cleared;
                            break;
                        }
                    }
                    if (cut) {
                        std::string error;
                        if (!fileops::move(step.source, step.destination,
                                           error)) {
                            all.status = fileops::Status::Failed;
                            all.message = error;
                            break;
                        }
                        ++all.files;
                        continue;
                    }
                    const fileops::Result part = fileops::copy(
                        step.source, step.destination, observer, done, bytes);
                    all.files += part.files;
                    if (part.status != fileops::Status::Ok) {
                        all.status = part.status;
                        all.message = part.message;
                        break;
                    }
                }
                return all;
            },
            settings_.keepAwake);
    }

    // Moved items are no longer where the clipboard points; anything that
    // did not move stays ready to try again.
    if (clipboardCut_) {
        clipboard_.erase(std::remove_if(clipboard_.begin(), clipboard_.end(),
                                        [](const std::string &path) {
                                            return !fileops::exists(path);
                                        }),
                         clipboard_.end());
    }
    if (result.status == fileops::Status::Failed) {
        problems.insert(problems.begin(), result.message);
    } else if (result.status == fileops::Status::Cancelled) {
        problems.insert(problems.begin(), "Stopped before it finished.");
    }
    if (!problems.empty() && ui::keepRunning()) {
        std::string list;
        for (const std::string &problem : problems) {
            list += (list.empty() ? "" : " ") + problem;
        }
        ui::message("Not everything was pasted", list, ui::Tone::Warning);
    }
    return true;
}

bool App::createFolder(const std::string &folder)
{
    std::string typed;
    if (!platform::keyboard(platform::KeyboardKind::Name, "New folder name",
                            "", typed)) {
        return false;
    }
    const std::string name = text::trim(typed);
    std::string error;
    if (!fileops::validName(name, error)) {
        ui::message("Cannot use that name", error, ui::Tone::Error, name);
        return false;
    }
    if (fileops::exists(folder + name)) {
        ui::message("Already exists", name + " is already in this folder.",
                    ui::Tone::Error);
        return false;
    }
    if (!fs::makeDirectories(folder + name + "/")) {
        ui::message("Could not create the folder", folder + name,
                    ui::Tone::Error);
        return false;
    }
    return true;
}

bool App::createFile(const std::string &folder)
{
    std::string typed;
    if (!platform::keyboard(platform::KeyboardKind::Name,
                            "New file name, for example config.ini",
                            "new.txt", typed)) {
        return false;
    }
    const std::string name = text::trim(typed);
    std::string error;
    if (!fileops::validName(name, error)) {
        ui::message("Cannot use that name", error, ui::Tone::Error, name);
        return false;
    }
    const std::string path = folder + name;
    if (fileops::exists(path)) {
        ui::message("Already exists", name + " is already in this folder.",
                    ui::Tone::Error);
        return false;
    }
    if (!fs::writeFile(path, "")) {
        ui::message("Could not create the file", path, ui::Tone::Error);
        return false;
    }
    textEditor(path);
    return true;
}

void App::showProperties(const std::vector<std::string> &paths)
{
    ui::Dialog dialog;
    dialog.header = "properties";
    if (paths.size() == 1 && !fileops::isFolder(paths.front())) {
        const std::string &path = paths.front();
        const int64_t size = fs::fileSize(path);
        const int64_t modified = fileops::modifiedTime(path);
        dialog.title = fileops::name(path);
        dialog.body.push_back(
            {filetype::label(filetype::fromName(basename(path))),
             ui::color::Muted, 0.46f, 1});
        dialog.body.push_back(
            {"Size: " + sizeText(size) + " (" + std::to_string(size) +
                 " bytes)",
             ui::color::Text, 0.46f, 1});
        if (modified > 0) {
            dialog.body.push_back({"Modified: " + units::dateTime(modified),
                                   ui::color::Text, 0.46f, 1});
        }
        dialog.body.push_back({path, ui::color::Muted, 0.42f, 3});
        ui::choose(dialog);
        return;
    }

    fileops::Totals totals;
    tasks::busy("properties", "Adding up sizes...", [&]() {
        for (const std::string &path : paths) {
            const fileops::Totals part = fileops::measure(path);
            totals.files += part.files;
            totals.folders += part.folders;
            totals.bytes += part.bytes;
        }
    });
    dialog.title = paths.size() == 1 ? fileops::name(paths.front()) + "/"
                                     : plural(paths.size(), "item");
    dialog.body.push_back({"Holds " + totalsText(totals) + ".",
                           ui::color::Text, 0.48f, 2});
    dialog.body.push_back({"That is " + std::to_string(totals.bytes) +
                               " bytes.",
                           ui::color::Muted, 0.44f, 1});
    if (paths.size() == 1) {
        const int64_t modified = fileops::modifiedTime(paths.front());
        if (modified > 0) {
            dialog.body.push_back({"Modified: " + units::dateTime(modified),
                                   ui::color::Text, 0.46f, 1});
        }
        dialog.body.push_back({paths.front(), ui::color::Muted, 0.42f, 3});
    }
    ui::choose(dialog);
}

// --- Viewers ----------------------------------------------------------------------

void App::installTicketFile(const std::string &path)
{
    if (!installer::available()) {
        ui::message("Cannot Install", installer::unavailableReason(),
                    ui::Tone::Error);
        return;
    }
    uint64_t titleId = 0;
    std::string error;
    bool installed = false;
    tasks::busy("Tickets", "Installing the ticket...", [&]() {
        installed = library::installTicket(path, titleId, error);
    });
    if (installed) {
        ui::message("Ticket Installed",
                    "The ticket for " + cia::titleIdText(titleId) +
                        " is installed.",
                    ui::Tone::Success);
    } else {
        ui::message("Ticket Not Installed", error, ui::Tone::Error);
    }
}

namespace {

// The title Luma3DS will swap for homebrew once 3dsDL closes: the Homebrew
// Launcher Loader unless Rosalina's "Switch the hb. title" picked another,
// such as Download Play. Luma keeps it in its config page, readable by
// every process (luma_shared_config.h, "selected_hbldr_3dsx_tid").
uint64_t homebrewTitle()
{
    return *reinterpret_cast<volatile const u64 *>(OS_SHAREDCFG_VADDR + 0x808);
}

// Asks Luma3DS's homebrew loader to start path once 3dsDL closes, the way
// the Homebrew Launcher starts what it lists.
bool startHomebrew(const std::string &path, std::string &error)
{
    // A global port, as the Homebrew Launcher opens it. Asking srv for it
    // waits forever, since Luma never registers it there.
    Handle loader = 0;
    if (R_FAILED(svcConnectToPort(&loader, "hb:ldr"))) {
        error = "Starting homebrew needs Luma3DS, and its homebrew loader "
                "did not answer.";
        return false;
    }
    // Under the Homebrew Launcher this app is the loader's own title, which
    // loads the target when it starts again. A CIA jumps to that title, and
    // libctru ignores a failed jump, which leaves 3dsDL hung on its last
    // frame, so it has to be installed.
    const uint64_t title = envIsHomebrew() ? 0 : homebrewTitle();
    uint16_t version = 0;
    if (!envIsHomebrew() &&
        (title == 0 || !installer::installedVersion(title, version))) {
        svcCloseHandle(loader);
        error = "Luma3DS starts homebrew in place of " +
                cia::titleIdText(title) +
                ", which is not installed. Open the Homebrew Launcher "
                "and start it from there.";
        return false;
    }
    // The loader takes the path inside the SD card, without "sdmc:".
    const std::string target =
        path.compare(0, 5, "sdmc:") == 0 ? path.substr(5) : path;
    u32 *command = getThreadCommandBuffer();
    command[0] = IPC_MakeHeader(2, 0, 2); // SetTarget
    command[1] = IPC_Desc_StaticBuffer(target.size() + 1, 0);
    command[2] = reinterpret_cast<u32>(target.c_str());
    ::Result code = svcSendSyncRequest(loader);
    if (R_SUCCEEDED(code)) {
        code = static_cast<::Result>(command[1]);
    }
    // Its arguments: a count, then each one ending in a zero byte. The
    // first is the program's own path, as the Homebrew Launcher passes it.
    std::vector<u32> arguments(0x400 / sizeof(u32), 0);
    if (R_SUCCEEDED(code) && path.size() < 0x400 - sizeof(u32)) {
        arguments[0] = 1;
        std::memcpy(&arguments[1], path.c_str(), path.size() + 1);
        command = getThreadCommandBuffer();
        command[0] = IPC_MakeHeader(3, 0, 2); // SetArgv
        command[1] = IPC_Desc_StaticBuffer(0x400, 1);
        command[2] = reinterpret_cast<u32>(arguments.data());
        code = svcSendSyncRequest(loader);
        if (R_SUCCEEDED(code)) {
            code = static_cast<::Result>(command[1]);
        }
    }
    svcCloseHandle(loader);
    if (R_FAILED(code)) {
        error = "The homebrew loader did not take " + basename(path) + ".";
        return false;
    }
    if (envIsHomebrew()) {
        aptSetChainloaderToSelf();
    } else {
        aptSetChainloader(title, installer::destination(title));
    }
    return true;
}

} // namespace

void App::titleDetails(const std::string &path, Kind kind)
{
    titlemeta::Meta meta;
    std::string error;
    const bool readable = titlemeta::read(path, kind, meta, error);
    const KindLook look = lookOf(kind);

    ui::ScreenSpec spec;
    spec.back = "Back";
    const bool ticket = kind == Kind::Ticket && readable;
    const bool homebrew = kind == Kind::Homebrew;
    ui::Control hex;
    hex.rect = {46, ticket || homebrew ? 150.0f : 110.0f, 228, 34};
    hex.label = "Open as Hex";
    hex.icon = ui::Icon::Hex;
    hex.scale = ui::type::Body + 0.06f;
    spec.controls.push_back(hex);
    if (ticket) {
        ui::Control install = hex;
        install.rect = {46, 106, 228, 34};
        install.label = "Install Ticket";
        install.icon = ui::Icon::Lock;
        install.iconColor = ui::color::IconGreen;
        spec.controls.push_back(install);
    }
    if (homebrew) {
        ui::Control start = hex;
        start.rect = {46, 106, 228, 34};
        start.label = "Start";
        start.icon = ui::Icon::App;
        start.iconColor = ui::color::IconGreen;
        spec.controls.push_back(start);
    }
    spec.drawTop = [&]() {
        ui::drawTopChrome(filetype::label(kind), look.icon, look.color);
        if (!readable) {
            ui::drawPanel();
            ui::drawPanelText(error, ui::TopPanel, ui::type::Body,
                              ui::color::Caution);
            return;
        }
        drawEntryPanel(meta.icon, kind,
                       meta.name.empty() ? basename(path) : meta.name,
                       meta.publisher.empty() ? basename(path) : meta.publisher,
                       {{"Title ID", meta.hasTitleId
                                         ? cia::titleIdText(meta.titleId)
                                         : ""},
                        {"Code", meta.productCode},
                        {"Size", sizeText(fs::fileSize(path))},
                        {"Note", meta.note}});
    };
    spec.drawBottom = [&]() {
        const ui::Rect panel = {13, 16, 294, 76};
        ui::drawPanel(panel);
        ui::drawPanelText(kind == Kind::Homebrew
                              ? "Start closes 3dsDL and opens it."
                          : kind == Kind::Ticket
                              ? "A ticket lets the console use a title's "
                                "content."
                              : basename(path),
                          panel, ui::type::Small + 0.04f);
    };
    int focus = ticket || homebrew ? 1 : 0;
    const int choice = ui::runScreen(spec, focus).choice;
    if (choice == 0) {
        hexViewer(path);
    } else if (choice == 1 && ticket) {
        installTicketFile(path);
    } else if (choice == 1 && homebrew) {
        std::string error;
        if (startHomebrew(path, error)) {
            quit_ = true;
        } else {
            ui::message("Cannot Start", error, ui::Tone::Error);
        }
    }
}

namespace {

// Touch buttons for screens that run their own loop: pressed on touch,
// chosen on release over the same button, as on the console.
struct Pad {
    std::vector<ui::Rect> rects;
    std::vector<bool> enabled;
    int pressed = -1;

    int add(const ui::Rect &rect, bool on = true)
    {
        rects.push_back(rect);
        enabled.push_back(on);
        return static_cast<int>(rects.size()) - 1;
    }
    int hit(const touchPosition &touch) const
    {
        for (size_t index = 0; index < rects.size(); ++index) {
            if (enabled[index] && rects[index].contains(touch)) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }
    // The button released on this frame, or -1.
    int update(const ui::Input &input)
    {
        if (input.down & KEY_TOUCH) {
            pressed = hit(input.touch);
        }
        if ((input.up & KEY_TOUCH) && pressed >= 0) {
            const int released = hit(input.touch) == pressed ? pressed : -1;
            pressed = -1;
            return released;
        }
        return -1;
    }
    bool isDown(const ui::Input &input, int index) const
    {
        return pressed == index && (input.held & KEY_TOUCH) &&
               rects[index].contains(input.touch);
    }
    // A touch that began on a button belongs to it, not to scrolling.
    bool owns(const ui::Input &input) const
    {
        return (input.down & KEY_TOUCH) && hit(input.touch) >= 0;
    }
};

// Smooth dragging and flicking in whole steps (lines, rows), for content
// too long to measure in pixels.
struct StepScroller {
    float carry = 0;
    float velocity = 0;
    bool dragging = false;
    float lastY = 0;

    // Returns how many steps to move this frame; positive goes down.
    int update(const ui::Input &input, bool owned, float stepPixels)
    {
        if ((input.down & KEY_TOUCH) && !owned) {
            dragging = true;
            lastY = input.touch.py;
            velocity = 0;
        }
        if (dragging && (input.held & KEY_TOUCH)) {
            const float delta = lastY - input.touch.py;
            lastY = input.touch.py;
            carry += delta;
            velocity = velocity * 0.4f + delta * 0.6f;
        } else {
            dragging = false;
            if (std::fabs(velocity) > 0.2f) {
                carry += velocity;
                velocity *= 0.94f;
            } else {
                velocity = 0;
            }
        }
        const int steps = static_cast<int>(carry / stepPixels);
        carry -= steps * stepPixels;
        return steps;
    }
    void stop()
    {
        velocity = 0;
        carry = 0;
    }
};

} // namespace

void App::pictureViewer(const std::vector<std::string> &pictures,
                        size_t index)
{
    if (pictures.empty()) {
        return;
    }
    Pad pad;
    const int previous = pad.add({16, 116, 140, 40}, pictures.size() > 1);
    const int next = pad.add({164, 116, 140, 40}, pictures.size() > 1);
    const int back = pad.add(ui::tabRect(ui::Tab::Left));

    while (ui::keepRunning()) {
        const std::string &path = pictures[index];
        picture::Picture shown;
        std::string error;
        picture::Picture loaded;
        tasks::busy("Pictures", "", [&]() {
            picture::load(path, 400, 240, PictureBackground, loaded, error);
        });
        shown = std::move(loaded);
        // Uploaded once; drawing it again every frame is cheap.
        const C2D_Image *image =
            error.empty()
                ? ui::showPicture(shown.pixels, shown.width, shown.height)
                : nullptr;

        float swipeStart = -1;
        bool moved = false;
        ui::enterScreen("picture");
        while (ui::keepRunning() && !moved) {
            const ui::Input input = ui::readInput();
            const int released = pad.update(input);
            if ((input.down & KEY_B) || released == back) {
                return;
            }
            int step = 0;
            if ((input.down & (KEY_LEFT | KEY_L)) || released == previous) {
                step = -1;
            }
            if ((input.down & (KEY_RIGHT | KEY_R)) || released == next) {
                step = 1;
            }
            // A sideways swipe anywhere else on the touch screen.
            if ((input.down & KEY_TOUCH) && !pad.owns(input)) {
                swipeStart = input.touch.px;
            }
            if ((input.up & KEY_TOUCH) && swipeStart >= 0) {
                const float distance = input.touch.px - swipeStart;
                if (std::fabs(distance) > 50) {
                    step = distance < 0 ? 1 : -1;
                }
                swipeStart = -1;
            }
            if (step != 0 && pictures.size() > 1) {
                index = (index + pictures.size() + step) % pictures.size();
                moved = true;
            }

            ui::beginFrame();
            ui::fillRound({0, 0, ui::TopWidth, ui::ScreenHeight}, 0,
                          ui::color::rgb(0x1A1C1E));
            if (image != nullptr) {
                ui::drawImage(*image, (ui::TopWidth - shown.width) / 2,
                              (ui::ScreenHeight - shown.height) / 2);
            } else {
                ui::drawWrapped(error.empty() ? "The picture could not be "
                                                "shown."
                                              : error,
                                200, 100, ui::type::Body,
                                ui::color::rgb(0xFFD36B), 330, 4,
                                C2D_AlignCenter);
            }

            ui::beginBottom();
            const std::string name = basename(path);
            const float width =
                26 + std::min(260.0f, ui::textWidth(name, 0.68f));
            ui::drawIcon(ui::Icon::Picture, 160 - width / 2, 9, 20,
                         ui::color::IconPink);
            ui::drawMarquee(name, 160 - width / 2 + 26, 9, 0.68f,
                            ui::color::Ink, 260);
            const ui::Rect panel = {13, 44, 294, 54};
            ui::drawPanel(panel);
            std::string facts = sizeText(fs::fileSize(path));
            if (shown.sourceWidth > 0) {
                facts = std::to_string(shown.sourceWidth) + " x " +
                        std::to_string(shown.sourceHeight) + " pixels, " +
                        facts;
            }
            ui::drawText(facts, 160, panel.y + 7, ui::type::Small,
                         ui::color::Ink, C2D_AlignCenter);
            ui::drawText("Picture " + std::to_string(index + 1) + " of " +
                             std::to_string(pictures.size()) +
                             " in this folder",
                         160, panel.y + 28, ui::type::Small,
                         ui::color::InkSoft, C2D_AlignCenter);
            ui::drawButton(pad.rects[previous], "Previous",
                           pictures.size() > 1 ? ui::Style::Normal
                                               : ui::Style::Disabled,
                           ui::type::Small + 0.02f, ui::Icon::None, 0,
                           pad.isDown(input, previous));
            ui::drawButton(pad.rects[next], "Next",
                           pictures.size() > 1 ? ui::Style::Normal
                                               : ui::Style::Disabled,
                           ui::type::Small + 0.02f, ui::Icon::None, 0,
                           pad.isDown(input, next));
            if (pictures.size() > 1) {
                ui::drawText("Swipe, or press L and R, for the others.", 160,
                             170, ui::type::Tiny, ui::color::InkSoft,
                             C2D_AlignCenter);
            }
            ui::drawTab(ui::Tab::Left, "Back", "", false,
                        pad.isDown(input, back));
            ui::endFrame();
        }
    }
}

void App::zipViewer(const std::string &path)
{
    zip::Archive archive;
    std::string error;
    bool ok = false;
    tasks::busy("ZIP Archive", "Reading " + basename(path) + "...",
                [&]() { ok = zip::readArchive(path, archive, error); });
    if (!ok) {
        ui::message("Cannot Open the Archive", error, ui::Tone::Error,
                    basename(path));
        return;
    }

    int cursor = 0;
    while (ui::keepRunning()) {
        ui::ListSpec spec;
        spec.title = basename(path);
        spec.emptyText = "This archive is empty.";
        spec.action = "Extract";
        spec.actionKey = "X";
        spec.actionKeys = KEY_X;
        for (const zip::Entry &entry : archive.entries) {
            ui::ListItem item;
            const KindLook look = lookOf(entry.directory
                                             ? Kind::Folder
                                             : filetype::fromName(entry.name));
            item.label = entry.name;
            item.detail = entry.directory ? "" : units::bytes(entry.size);
            item.icon = look.icon;
            item.iconColor = look.color;
            if (entry.path.empty()) {
                item.labelColor = ui::color::Bad;
            }
            spec.items.push_back(item);
        }
        spec.drawTop = [&](int index) {
            ui::drawTopChrome(basename(path), ui::Icon::Zip,
                              ui::color::IconGold);
            ui::drawPanel();
            const float left = ui::TopPanel.x + 18;
            float y = ui::TopPanel.y + 16;
            y = ui::drawTagRow(left, y, 92, "Holds",
                               plural(archive.fileCount, "file"));
            y = ui::drawTagRow(left, y, 92, "Extracted",
                               units::bytes(archive.totalSize));
            if (index >= 0 && index < static_cast<int>(archive.entries.size())) {
                const zip::Entry &entry = archive.entries[index];
                y = ui::drawTagRow(left, y, 92, "Selected", entry.name);
                if (entry.path.empty()) {
                    ui::drawTagRow(left, y, 92, "Skipped",
                                   "Its path would leave the folder.",
                                   ui::color::Bad);
                }
            }
            ui::drawWrapped("Paths that would leave the folder are "
                            "skipped.",
                            left, ui::TopPanel.y + ui::TopPanel.h - 48,
                            ui::type::Small, ui::color::InkSoft,
                            ui::TopPanel.w - 36, 2);
        };
        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key == KEY_X) {
            offerExtraction(path, folderOf(path), "",
                            "Extract " + basename(path) + "?");
            return;
        }
        if (result.key != KEY_A) {
            return;
        }
    }
}

void App::hexViewer(const std::string &path)
{
    constexpr int Columns = 8;
    constexpr int Rows = 12;
    constexpr uint64_t Page = Columns * Rows;
    constexpr float RowPitch = 14.5f;
    const int64_t fileSize = fs::fileSize(path);
    const uint64_t size = fileSize > 0 ? static_cast<uint64_t>(fileSize) : 0;
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ui::message("Cannot Open the File", path, ui::Tone::Error);
        return;
    }

    Pad pad;
    const int pageUp = pad.add({16, 72, 140, 32});
    const int pageDown = pad.add({164, 72, 140, 32});
    const int start = pad.add({16, 114, 140, 32});
    const int end = pad.add({164, 114, 140, 32});
    const int back = pad.add(ui::tabRect(ui::Tab::Left));
    const uint64_t lastTop =
        size > Page ? ((size - Page + Columns - 1) / Columns) * Columns : 0;
    StepScroller scroller;

    uint64_t top = 0;
    uint64_t loadedAt = UINT64_MAX;
    std::vector<uint8_t> bytes(Page);
    size_t count = 0;
    const auto moveRows = [&](int64_t rows) {
        const int64_t goal = static_cast<int64_t>(top) + rows * Columns;
        top = static_cast<uint64_t>(
            std::max<int64_t>(0, std::min<int64_t>(goal, lastTop)));
    };
    ui::enterScreen("hex");
    while (ui::keepRunning()) {
        const ui::Input input = ui::readInput();
        const int released = pad.update(input);
        if ((input.down & KEY_B) || released == back) {
            break;
        }
        if (input.repeat & KEY_DOWN) {
            moveRows(1);
        }
        if (input.repeat & KEY_UP) {
            moveRows(-1);
        }
        if ((input.repeat & (KEY_RIGHT | KEY_R)) || released == pageDown) {
            moveRows(Rows);
        }
        if ((input.repeat & (KEY_LEFT | KEY_L)) || released == pageUp) {
            moveRows(-Rows);
        }
        if (released == start) {
            top = 0;
            scroller.stop();
        }
        if (released == end) {
            top = lastTop;
            scroller.stop();
        }
        moveRows(scroller.update(input, pad.owns(input), RowPitch));
        if (loadedAt != top) {
            count = 0;
            if (fseeko(file, static_cast<off_t>(top), SEEK_SET) == 0) {
                count = std::fread(bytes.data(), 1, bytes.size(), file);
            }
            loadedAt = top;
        }

        ui::beginFrame();
        ui::drawStatusBar();
        const std::string name = basename(path);
        const float width =
            28 + std::min(300.0f, ui::textWidth(name, ui::type::Title));
        ui::drawIcon(ui::Icon::Hex, 200 - width / 2, 22, 22, ui::color::IconGrey);
        ui::drawMarquee(name, 200 - width / 2 + 28, 21, ui::type::Title,
                        ui::color::Accent, 300);
        const ui::Rect paper = {10, 50, 380, 184};
        ui::fillRound({paper.x - 1, paper.y - 1, paper.w + 2, paper.h + 2}, 7,
                      ui::color::rgb(0xCFC48A));
        ui::fillRound(paper, 6, ui::color::rgb(0xFFFDF4));
        for (int row = 0; row < Rows; ++row) {
            const size_t first = static_cast<size_t>(row) * Columns;
            if (first >= count) {
                break;
            }
            const float y = paper.y + 5 + row * RowPitch;
            char offset[16];
            std::snprintf(offset, sizeof(offset), "%08llX",
                          static_cast<unsigned long long>(top + first));
            ui::drawText(offset, 20, y, 0.34f, ui::color::rgb(0xA79D70));
            for (int column = 0; column < Columns; ++column) {
                if (first + column >= count) {
                    break;
                }
                const uint8_t value = bytes[first + column];
                char hex[4];
                std::snprintf(hex, sizeof(hex), "%02X", value);
                ui::drawText(hex, 100 + column * 25 + (column >= 4 ? 8 : 0), y,
                             0.34f, ui::color::InkStrong);
                const char shown[2] = {
                    value >= 0x20 && value < 0x7f ? static_cast<char>(value)
                                                  : '.',
                    0};
                ui::drawText(shown, 318 + column * 8.5f, y, 0.34f,
                             ui::color::IconGreen);
            }
        }

        ui::beginBottom();
        const ui::Rect panel = {13, 10, 294, 50};
        ui::drawPanel(panel);
        char where[96];
        std::snprintf(where, sizeof(where), "Offset 0x%llX of %s",
                      static_cast<unsigned long long>(top),
                      units::bytes(size).c_str());
        ui::drawText(where, 160, panel.y + 6, ui::type::Small, ui::color::Ink,
                     C2D_AlignCenter);
        ui::drawText("Drag or flick to scroll, L and R for a page.", 160,
                     panel.y + 27, ui::type::Tiny, ui::color::InkSoft,
                     C2D_AlignCenter);
        ui::drawButton(pad.rects[pageUp], "Page Up", ui::Style::Normal,
                       ui::type::Small + 0.02f, ui::Icon::None, 0,
                       pad.isDown(input, pageUp));
        ui::drawButton(pad.rects[pageDown], "Page Down", ui::Style::Normal,
                       ui::type::Small + 0.02f, ui::Icon::None, 0,
                       pad.isDown(input, pageDown));
        ui::drawButton(pad.rects[start], "Go to Start", ui::Style::Normal,
                       ui::type::Small + 0.02f, ui::Icon::None, 0,
                       pad.isDown(input, start));
        ui::drawButton(pad.rects[end], "Go to End", ui::Style::Normal,
                       ui::type::Small + 0.02f, ui::Icon::None, 0,
                       pad.isDown(input, end));
        ui::drawTab(ui::Tab::Left, "Back", "", false, pad.isDown(input, back));
        ui::endFrame();
    }
    std::fclose(file);
}

void App::textEditor(const std::string &path)
{
    textdoc::Document document;
    switch (textdoc::load(path, document)) {
    case textdoc::Load::Unreadable:
        ui::message("Cannot Open the File", path, ui::Tone::Error);
        return;
    case textdoc::Load::NotText: {
        ui::Dialog dialog;
        dialog.header = basename(path);
        dialog.title = "This is not a text file.";
        dialog.body.push_back({"It holds binary data. The hex view shows it "
                               "byte by byte.",
                               ui::color::Text, 0.48f, 3});
        dialog.buttons = {"Close", "Open as Hex"};
        dialog.initial = 1;
        dialog.cancelButton = 0;
        if (ui::choose(dialog) == 1) {
            hexViewer(path);
        }
        return;
    }
    case textdoc::Load::Ok:
        break;
    }

    // A new, empty file gets the usual line break after its last line.
    if (fs::fileSize(path) == 0) {
        document.finalNewline = true;
    }
    const std::string extension = text::lower(naming::extension(path));
    const bool readOnly = document.readOnly;
    std::string readOnlyReason;
    if (document.truncated) {
        readOnlyReason = "Only the first " +
                         units::bytes(textdoc::MaxEditableBytes) +
                         " is shown, so it cannot be edited here.";
    } else if (readOnly) {
        readOnlyReason = "It is not UTF-8 text, so editing could change "
                         "characters you cannot see.";
    }

    constexpr float LinePitch = 16;
    constexpr float EditorScale = 0.38f;
    const ui::Rect paper = {10, 50, 380, 184};
    const float viewport = paper.h - 4;
    int cursor = 0;
    bool dirty = false;
    bool saved = false; // shows "saved" until the next change
    ui::Scroller scroller;

    Pad pad;
    const int edit = pad.add({13, 92, 94, 34}, !readOnly);
    const int insert = pad.add({113, 92, 94, 34}, !readOnly);
    const int remove = pad.add({213, 92, 94, 34}, !readOnly);
    const int close = pad.add(ui::tabRect(ui::Tab::Left));
    const int save = pad.add(ui::tabRect(ui::Tab::Right), !readOnly);

    const auto store = [&]() {
        if (textdoc::save(path, document)) {
            dirty = false;
            saved = true;
            return true;
        }
        ui::message("Not Saved", "The file could not be written. The SD card "
                                 "may be full or read-only.",
                    ui::Tone::Error, path);
        return false;
    };
    const auto editLine = [&](int index, bool inserting) {
        std::string typed;
        const std::string initial = inserting ? "" : document.lines[index];
        if (initial.size() > 1900) {
            ui::message("Line Too Long",
                        "This line is too long for the keyboard.",
                        ui::Tone::Warning);
            return;
        }
        if (!platform::keyboard(platform::KeyboardKind::Text,
                                "Line " + std::to_string(index + 1), initial,
                                typed)) {
            return;
        }
        const std::vector<std::string> lines = splitLines(typed);
        if (inserting) {
            document.lines.insert(document.lines.begin() + index,
                                  lines.begin(), lines.end());
        } else {
            document.lines.erase(document.lines.begin() + index);
            document.lines.insert(document.lines.begin() + index,
                                  lines.begin(), lines.end());
        }
        cursor = index + static_cast<int>(lines.size()) - 1;
        scroller.reveal(cursor * LinePitch, LinePitch, viewport);
        dirty = true;
        saved = false;
    };

    ui::enterScreen("editor");
    while (ui::keepRunning()) {
        const ui::Input input = ui::readInput();
        const int count = static_cast<int>(document.lines.size());
        const int visible = static_cast<int>(viewport / LinePitch);
        const int released = pad.update(input);
        int action = released;
        if (input.down & KEY_A) {
            action = edit;
        } else if (input.down & KEY_X) {
            action = insert;
        } else if (input.down & KEY_Y) {
            action = remove;
        } else if (input.down & KEY_START) {
            action = save;
        } else if (input.down & KEY_B) {
            action = close;
        }
        int moved = cursor;
        if (input.repeat & KEY_DOWN) {
            moved = std::min(count - 1, cursor + 1);
        }
        if (input.repeat & KEY_UP) {
            moved = std::max(0, cursor - 1);
        }
        if (input.repeat & (KEY_RIGHT | KEY_R)) {
            moved = std::min(count - 1, cursor + visible);
        }
        if (input.repeat & (KEY_LEFT | KEY_L)) {
            moved = std::max(0, cursor - visible);
        }
        if (moved != cursor) {
            cursor = moved;
            scroller.reveal(cursor * LinePitch, LinePitch, viewport);
        }
        // Dragging anywhere on the touch screen but the buttons scrolls the
        // page on the top screen; the cursor stays on a visible line.
        if (!pad.owns(input)) {
            scroller.update(input, {0, 0, 320, 209}, count * LinePitch,
                            viewport);
        }
        if (scroller.dragging() || scroller.offset() != 0) {
            const int firstLine = static_cast<int>(
                std::ceil(std::max(0.0f, scroller.offset()) / LinePitch));
            const int lastLine = static_cast<int>(
                (std::max(0.0f, scroller.offset()) + viewport) / LinePitch) - 1;
            if (scroller.dragging() || scroller.moved()) {
                cursor = std::max(firstLine, std::min(cursor, std::max(firstLine, lastLine)));
                cursor = std::min(cursor, count - 1);
            }
        }

        if (readOnly && (action == edit || action == insert ||
                         action == remove || action == save)) {
            ui::message("Read-Only", readOnlyReason);
            action = -1;
        }
        if (action == edit) {
            editLine(cursor, false);
        } else if (action == insert) {
            editLine(cursor + 1, true);
        } else if (action == remove) {
            if (document.lines.size() > 1) {
                document.lines.erase(document.lines.begin() + cursor);
                cursor = std::min(cursor,
                                  static_cast<int>(document.lines.size()) - 1);
            } else {
                document.lines[0].clear();
            }
            dirty = true;
            saved = false;
        } else if (action == save) {
            store();
        } else if (action == close) {
            if (!dirty) {
                return;
            }
            ui::Dialog dialog;
            dialog.header = basename(path);
            dialog.title = "Save your changes before closing?";
            dialog.tone = ui::Tone::Warning;
            dialog.buttons = {"Discard", "Keep Editing", "Save"};
            dialog.dangerous = 0;
            dialog.initial = 2;
            dialog.cancelButton = 1;
            const int answer = ui::choose(dialog);
            if (answer == 0 || (answer == 2 && store())) {
                return;
            }
        }

        ui::beginFrame();
        const std::string name = basename(path);
        const float nameWidth = std::min(260.0f, ui::textWidth(name, ui::type::Title));
        const std::string state = dirty      ? "edited"
                                : saved    ? "saved"
                                : readOnly ? "read-only"
                                           : "";
        const float width = 28 + nameWidth +
                            (state.empty() ? 0 : 8 + ui::textWidth(state, ui::type::Small));
        // A ruled page with a line number gutter.
        ui::fillRound({paper.x - 1, paper.y - 1, paper.w + 2, paper.h + 2}, 7,
                      ui::color::rgb(0xCFC48A));
        ui::fillRound(paper, 6, ui::color::rgb(0xFFFDF4));
        const float offset = scroller.offset();
        const int first = std::max(0, static_cast<int>(offset / LinePitch));
        ui::fillRound({paper.x, paper.y, 30, paper.h}, 6,
                      ui::color::rgb(0xEDE6C0), ui::corner::TopLeft |
                                                    ui::corner::BottomLeft);
        for (int index = first; index < count; ++index) {
            const float y = paper.y + 2 + index * LinePitch - offset;
            if (y > paper.y + paper.h) {
                break;
            }
            if (y + LinePitch < paper.y) {
                continue;
            }
            if (index == cursor) {
                ui::fillRound({paper.x + 1, y, paper.w - 2, LinePitch}, 2,
                              {{0, ui::color::rgb(0xD6EEF8)},
                               {1, ui::color::rgb(0xBFE3F3)}});
            }
            ui::fillRound({paper.x + 30, y + LinePitch - 1, paper.w - 31, 1},
                          0, ui::color::rgb(0xE8E1BE));
            ui::drawText(std::to_string(index + 1), paper.x + 26, y + 2,
                         0.3f, ui::color::rgb(0xA79D70), C2D_AlignRight);
            const std::string &line = document.lines[index];
            // The current line scrolls to show all of it; the rest are cut.
            if (index == cursor && !scroller.dragging()) {
                ui::drawMarquee(line, paper.x + 36, y, EditorScale,
                                lineColor(line, extension), paper.w - 44);
            } else {
                ui::drawText(ui::fitEnd(line, EditorScale, paper.w - 44),
                             paper.x + 36, y, EditorScale,
                             lineColor(line, extension));
            }
        }
        // Lines scrolled past the page are covered again by the chrome.
        ui::fillRound({0, 0, ui::TopWidth, paper.y - 1}, 0, ui::color::TopA);
        ui::drawStatusBar();
        ui::drawIcon(ui::Icon::Text, 200 - width / 2, 22, 22, ui::color::IconGrey);
        ui::drawMarquee(name, 200 - width / 2 + 28, 21, ui::type::Title,
                        ui::color::Accent, 260);
        if (!state.empty()) {
            ui::drawText(state, 200 - width / 2 + 28 + nameWidth + 8,
                         26, ui::type::Small,
                         dirty ? ui::color::rgb(0xE07A10) : ui::color::InkSoft);
        }
        ui::fillRound({0, paper.y + paper.h + 1, ui::TopWidth,
                       ui::ScreenHeight - paper.y - paper.h - 1},
                      0, ui::color::TopB);

        ui::beginBottom();
        const ui::Rect recess = {13, 8, 294, 72};
        ui::drawRecess(recess);
        ui::drawText("Line " + std::to_string(cursor + 1) + " of " +
                         std::to_string(count),
                     recess.x + 10, recess.y + 5, ui::type::Tiny,
                     ui::color::InkSoft);
        const std::string &current = document.lines[cursor];
        ui::drawWrapped(current.empty() ? "(empty line)" : current,
                        recess.x + 10, recess.y + 24, ui::type::Small,
                        current.empty() ? ui::color::InkSoft
                                        : ui::color::InkStrong,
                        recess.w - 20, 2);
        const char *const labels[] = {"Edit", "New Line", "Delete"};
        for (int index = 0; index < 3; ++index) {
            ui::drawButton(pad.rects[index], labels[index],
                           readOnly ? ui::Style::Disabled : ui::Style::Normal,
                           ui::type::Small, ui::Icon::None, 0,
                           pad.isDown(input, index));
        }
        if (!readOnly) {
            ui::drawFocus(pad.rects[edit], 9);
        }
        ui::drawText(readOnly ? readOnlyReason
                              : "A Edit   X New line   Y Delete",
                     160, 140, ui::type::Tiny, ui::color::InkSoft,
                     C2D_AlignCenter);
        ui::drawText("Drag or flick here to scroll the page.", 160, 160,
                     ui::type::Tiny, ui::color::Counter, C2D_AlignCenter);
        ui::drawTab(ui::Tab::Left, "Close", "", false, pad.isDown(input, close));
        if (!readOnly) {
            ui::drawTab(ui::Tab::Right, "Save", "START", false,
                        pad.isDown(input, save));
        }
        ui::endFrame();
    }
}
