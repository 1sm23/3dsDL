#include "app.hpp"

#include "app_util.hpp"
#include "cia.hpp"
#include "file_ops.hpp"
#include "file_types.hpp"
#include "fs_util.hpp"
#include "installer.hpp"
#include "naming.hpp"
#include "picture.hpp"
#include "platform.hpp"
#include "storage.hpp"
#include "tasks.hpp"
#include "text.hpp"
#include "text_doc.hpp"
#include "title_meta.hpp"
#include "ui.hpp"
#include "units.hpp"
#include "zip.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>

namespace {

using filetype::Kind;
using screens::basename;
using screens::ciaName;
using screens::drawLabelledPanel;
using screens::plural;

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

u32 kindColor(Kind kind, const std::string &name)
{
    if (!name.empty() && name[0] == '.') {
        return ui::color::Muted;
    }
    switch (kind) {
    case Kind::Cia:
    case Kind::Homebrew:
    case Kind::CartridgeImage:
    case Kind::Nds:
        return ui::color::Primary;
    case Kind::Zip:
        return ui::color::Warning;
    case Kind::Image:
        return ui::color::Success;
    default:
        return ui::color::Text;
    }
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

fs::SortOrder nextSort(fs::SortOrder order)
{
    switch (order) {
    case fs::SortOrder::Name:
        return fs::SortOrder::Size;
    case fs::SortOrder::Size:
        return fs::SortOrder::Date;
    case fs::SortOrder::Date:
        return fs::SortOrder::Type;
    case fs::SortOrder::Type:
        break;
    }
    return fs::SortOrder::Name;
}

std::string sizeText(int64_t size)
{
    return size >= 0 ? units::bytes(static_cast<uint64_t>(size)) : "";
}

// A pick-one list that returns the chosen index, or -1.
struct MenuItem {
    std::string label;
    std::string detail;
    u32 color = ui::color::Text;
};

int menu(const std::string &header, const std::string &title,
         const std::vector<MenuItem> &items,
         const std::function<void(int)> &drawTop)
{
    ui::ListSpec spec;
    spec.header = header;
    spec.title = title;
    spec.hint = "A: Choose    B: Back";
    for (const MenuItem &item : items) {
        spec.items.push_back({item.label, item.detail, item.color});
    }
    spec.drawTop = drawTop;
    int cursor = 0;
    const ui::ListResult result = ui::runList(spec, cursor);
    return result.key == KEY_A ? result.index : -1;
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
    dialog.buttons = {yes, "CANCEL"};
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
};

Details loadDetails(const std::string &path, Kind kind)
{
    Details details;
    switch (kind) {
    case Kind::Folder:
        details.items = fs::listEntries(path + "/", nullptr, true).size();
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

void drawRows(float y, const std::vector<std::pair<std::string, std::string>>
                           &rows)
{
    for (const auto &row : rows) {
        if (row.second.empty() || y > 210) {
            continue;
        }
        ui::drawText(row.first, 34, y, 0.44f, ui::color::Muted);
        ui::drawText(ui::fitEnd(row.second, 0.46f, 252), 118, y, 0.46f,
                     ui::color::Text);
        y += 18;
    }
}

// The header block with an optional icon: name and a second line.
void drawTitleBlock(const std::vector<uint16_t> &icon,
                    const std::string &name, const std::string &second)
{
    float x = 34;
    if (const C2D_Image *image = ui::showIcon(icon)) {
        C2D_DrawImageAt(*image, 34, 64, 0.5f);
        x = 94;
    }
    ui::drawText(ui::fitEnd(name, 0.6f, 366 - x), x, 66, 0.6f,
                 ui::color::Text);
    ui::drawText(ui::fitEnd(second, 0.44f, 366 - x), x, 90, 0.44f,
                 ui::color::Muted);
}

void drawEntryTop(const std::string &path, const fs::Entry &entry,
                  const Details &details)
{
    const Kind kind = kindOf(entry);
    const std::string modified =
        entry.modified > 0 ? units::dateTime(entry.modified) : "";
    ui::drawPanel({18, 54, 364, 172});

    if (kind == Kind::Folder) {
        drawTitleBlock({}, entry.name + "/", path + "/");
        drawRows(122, {{"Type", "Folder"},
                       {"Contains", plural(details.items, "item")},
                       {"Modified", modified}});
        return;
    }
    if (kind == Kind::Cia && details.ciaReadable) {
        const cia::Info &info = details.cia;
        drawTitleBlock(info.smdh.icon, ciaName(info),
                       info.smdh.publisher.empty() ? entry.name
                                                   : info.smdh.publisher);
        drawRows(122,
                 {{"Type", cia::kindName(cia::kind(info.titleId))},
                  {"Title ID", cia::titleIdText(info.titleId)},
                  {"Version", cia::versionText(info.version)},
                  {"Installed", details.installed
                                    ? cia::versionText(details.installedVersion)
                                    : "No"},
                  {"Size", units::bytes(info.fileSize)}});
        return;
    }
    if (details.metaReadable) {
        const titlemeta::Meta &meta = details.meta;
        drawTitleBlock(meta.icon, meta.name.empty() ? entry.name : meta.name,
                       meta.publisher.empty() ? filetype::label(kind)
                                              : meta.publisher);
        drawRows(122, {{"Type", filetype::label(kind)},
                       {"Title ID", meta.hasTitleId
                                        ? cia::titleIdText(meta.titleId)
                                        : ""},
                       {"Code", meta.productCode},
                       {"Size", sizeText(entry.size)},
                       {"Note", meta.note}});
        return;
    }

    drawTitleBlock({}, entry.name, filetype::label(kind));
    if (!details.head.empty()) {
        // The start of a text file.
        float y = 112;
        for (const std::string &line : details.head) {
            ui::drawText(ui::fitEnd(line, 0.42f, 330), 34, y, 0.42f,
                         ui::color::Muted);
            y += 12;
        }
        return;
    }
    drawRows(122,
             {{"Type", filetype::label(kind)},
              {"Picture", details.width > 0
                              ? std::to_string(details.width) + " x " +
                                    std::to_string(details.height) + " pixels"
                              : ""},
              {"Size", sizeText(entry.size)},
              {"Modified", modified},
              {"Problem", details.error}});
}

// --- Text editor helpers -------------------------------------------------------

constexpr float EditorScale = 0.44f;

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

struct TouchButton {
    ui::Rect rect;
    std::string label;
    bool enabled = true;
};

void drawTouchButtons(const std::vector<TouchButton> &buttons)
{
    for (const TouchButton &button : buttons) {
        C2D_DrawRectSolid(button.rect.x, button.rect.y, 0.4f, button.rect.w,
                          button.rect.h, ui::color::SurfaceRaised);
        ui::drawText(ui::fitEnd(button.label, 0.46f, button.rect.w - 8),
                     button.rect.x + button.rect.w / 2,
                     button.rect.y + button.rect.h / 2 -
                         ui::lineHeight(0.46f) / 2,
                     0.46f,
                     button.enabled ? ui::color::Text : ui::color::Muted,
                     C2D_AlignCenter);
    }
}

int tappedButton(const ui::Input &input,
                 const std::vector<TouchButton> &buttons)
{
    for (size_t index = 0; index < buttons.size(); ++index) {
        if (buttons[index].enabled && input.tapped(buttons[index].rect)) {
            return static_cast<int>(index);
        }
    }
    return -1;
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

    while (ui::keepRunning()) {
        bool readable = false;
        std::vector<fs::Entry> entries =
            fs::listEntries(current, &readable, settings_.showHiddenFiles);
        if (!readable && current != "sdmc:/") {
            current = "sdmc:/";
            cursor = 0;
            marked.clear();
            continue;
        }
        fs::sortEntries(entries, settings_.fileSort);
        const bool atRoot = naming::parentFolder(current) == current;
        const int first = atRoot ? 0 : 1;

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
        spec.header = !marked.empty() ? plural(marked.size(), "marked item")
                    : !clipboard_.empty()
                        ? plural(clipboard_.size(), "item") +
                              (clipboardCut_ ? " to move" : " to copy")
                        : "files";
        spec.title = ui::fitMiddle(current, 0.56f, 290);
        spec.emptyText = readable
            ? "This folder is empty. SELECT or FOLDER makes a new file or "
              "folder here."
            : "The SD card could not be read.";
        spec.extraKeys = KEY_X | KEY_Y | KEY_SELECT | KEY_START;
        spec.buttons = {{"UP", KEY_B},
                        {"MARK", KEY_Y},
                        {"OPTIONS", KEY_X},
                        {"FOLDER", KEY_SELECT}};
        if (!atRoot) {
            spec.items.push_back({".. (up one level)", "", ui::color::Muted});
        }
        for (const fs::Entry &entry : entries) {
            ui::ListItem item;
            item.label = entry.folder ? entry.name + "/" : entry.name;
            item.detail = entry.folder ? "" : sizeText(entry.size);
            item.labelColor = kindColor(kindOf(entry), entry.name);
            item.marked = markedSet.count(current + entry.name) > 0;
            spec.items.push_back(item);
        }

        spec.drawTop = [&](int index) {
            const int position = index - first;
            if (position < 0 ||
                position >= static_cast<int>(entries.size())) {
                drawLabelledPanel(56, 74, "BROWSING", current,
                                  ui::color::Text);
                std::string summary = plural(entries.size(), "item") +
                                      ". Sorted by " +
                                      text::lower(sortLabel(settings_.fileSort)) +
                                      ".";
                if (!clipboard_.empty()) {
                    summary += " " + plural(clipboard_.size(), "item") +
                               " ready to paste with FOLDER.";
                }
                drawLabelledPanel(144, 76, "CONTENTS", summary,
                                  ui::color::Text);
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
        if (result.key == KEY_B ||
            (result.key == KEY_A && !atRoot && result.index == 0)) {
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
            const std::vector<std::string> targets =
                marked.empty() ? std::vector<std::string>{highlighted} : marked;
            if (entryOptions(current, targets)) {
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
            fs::listEntries(folder, nullptr, settings_.showHiddenFiles);
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
        dialog.header = "cia";
        dialog.title = basename(path);
        dialog.tone = ui::Tone::Warning;
        dialog.body.push_back({"Not installable: " + error,
                               ui::color::Warning, 0.48f, 3});
        dialog.buttons = {"VIEW AS HEX", "CLOSE"};
        dialog.cancelButton = 1;
        if (ui::choose(dialog) == 0) {
            hexViewer(path);
        }
        return;
    }

    ui::Dialog dialog;
    dialog.header = "cia";
    dialog.title = ciaName(info);
    dialog.icon = ui::showIcon(info.smdh.icon);
    screens::addCiaParagraphs(dialog, info);
    if (!installer::available()) {
        dialog.body.push_back({installer::unavailableReason(),
                               ui::color::Warning, 0.42f, 3});
    }
    dialog.buttons = {"INSTALL", "INSTALL & DELETE", "CLOSE"};
    dialog.cancelButton = 2;
    const int choice = ui::choose(dialog);
    if (choice == 0 || choice == 1) {
        installCia(path, info, choice == 1, "");
    }
}

// --- Options --------------------------------------------------------------------

bool App::entryOptions(const std::string &folder,
                       const std::vector<std::string> &paths)
{
    if (paths.empty() || paths.front().empty()) {
        return false;
    }
    enum class Action {
        Open,
        Install,
        InstallDelete,
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
        Properties
    };
    std::vector<MenuItem> items;
    std::vector<Action> actions;
    const auto add = [&](const std::string &label, Action action,
                         u32 color = ui::color::Text) {
        items.push_back({label, "", color});
        actions.push_back(action);
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
        for (const fs::Entry &entry : fs::listEntries(path + "/")) {
            if (!entry.folder && kindOf(entry) == Kind::Cia) {
                cias.push_back(path + "/" + entry.name);
            }
        }
    }

    if (single) {
        add(isFolder ? "Open folder" : "Open", Action::Open, ui::color::Primary);
        if (kind == Kind::Cia) {
            add("Install", Action::Install);
            add("Install & delete the file", Action::InstallDelete);
        }
        if (kind == Kind::Zip) {
            add("Extract...", Action::Extract);
        }
        if (!isFolder) {
            if (kind == Kind::Text) {
                add("Edit", Action::Edit);
            } else {
                add("Open as text", Action::ViewText);
            }
            add("Open as hex", Action::ViewHex);
        }
        add("Rename", Action::Rename);
    }
    if (!single || isFolder) {
        if (!cias.empty()) {
            add("Install " + plural(cias.size(), "CIA"), Action::InstallAll);
            add("Install & delete " + plural(cias.size(), "CIA"),
                Action::InstallDeleteAll);
        }
    }
    const std::string count = single ? "" : " " + plural(paths.size(), "item");
    add("Copy" + count, Action::Copy);
    add("Cut" + count, Action::Cut);
    add("Delete" + count, Action::Delete, ui::color::Danger);
    add("Properties", Action::Properties);

    const int choice = menu(
        "options",
        single ? basename(path) : plural(paths.size(), "marked item"), items,
        [&](int) {
            drawLabelledPanel(56, 74, single ? filetype::label(kind) : "MARKED",
                              single ? path
                                     : plural(paths.size(), "item") + " in " +
                                           folder,
                              ui::color::Text);
            if (!clipboard_.empty()) {
                drawLabelledPanel(144, 76, "CLIPBOARD",
                                  plural(clipboard_.size(), "item") +
                                      (clipboardCut_ ? " to move" : " to copy"),
                                  ui::color::Muted);
            }
        });
    if (choice < 0) {
        return false;
    }

    switch (actions[choice]) {
    case Action::Open:
        if (isFolder) {
            filesFolder_ = path + "/";
        } else {
            openFile(path);
        }
        return true;
    case Action::Install:
    case Action::InstallDelete:
        installCias({path}, actions[choice] == Action::InstallDelete);
        return true;
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
        ui::message(clipboardCut_ ? "Ready to move" : "Ready to copy",
                    plural(paths.size(), "item") +
                        (clipboardCut_ ? " will move" : " will be copied") +
                        " when you choose Paste. Open the folder to put "
                        "them in, then press SELECT or FOLDER.");
        return true;
    case Action::Delete:
        return deleteEntries(paths);
    case Action::Properties:
        showProperties(paths);
        return false;
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
    std::vector<MenuItem> items;
    std::vector<Action> actions;
    const auto add = [&](const std::string &label, const std::string &detail,
                         Action action, u32 color = ui::color::Text) {
        items.push_back({label, detail, color});
        actions.push_back(action);
    };

    if (!clipboard_.empty()) {
        add(std::string(clipboardCut_ ? "Move " : "Paste ") +
                plural(clipboard_.size(), "item") + " here",
            "", Action::Paste, ui::color::Primary);
        add("Forget the copied items", "", Action::ClearClipboard);
    }
    add("New folder", "", Action::NewFolder);
    add("New file", "", Action::NewFile);
    add("Sort by", sortLabel(settings_.fileSort), Action::Sort);
    add("Show hidden files", settings_.showHiddenFiles ? "On" : "Off",
        Action::Hidden);
    if (!entries.empty()) {
        add("Mark everything", "", Action::MarkAll);
    }
    if (!marked.empty()) {
        add("Clear marks", plural(marked.size(), "item"), Action::ClearMarks);
    }
    size_t cias = 0;
    for (const std::string &entry : entries) {
        if (!fileops::isFolder(entry) &&
            filetype::fromName(basename(entry)) == Kind::Cia) {
            ++cias;
        }
    }
    if (cias > 0) {
        add("Install every CIA here", plural(cias, "CIA"), Action::InstallAll);
    }
    add("Folder properties", "", Action::Properties);

    const int choice = menu("folder", ui::fitMiddle(folder, 0.56f, 290), items,
                            [&](int) {
                                drawLabelledPanel(56, 74, "FOLDER", folder,
                                                  ui::color::Text);
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
    case Action::Sort:
        settings_.fileSort = nextSort(settings_.fileSort);
        persistSettings();
        return true;
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
        dialog.buttons = {"INSTALL", "INSTALL & DELETE", "CANCEL"};
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
    if (!confirm("delete", "Delete " + what + "?", body, "DELETE", true)) {
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
                dialog.title = name + " is already here";
                dialog.tone = ui::Tone::Warning;
                dialog.body.push_back({"KEEP BOTH adds a number to the new "
                                       "one's name. REPLACE deletes the one "
                                       "already in " + folder + " first.",
                                       ui::color::Text, 0.46f, 4});
                dialog.buttons = {"KEEP BOTH", "REPLACE", "SKIP"};
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

void App::titleDetails(const std::string &path, Kind kind)
{
    titlemeta::Meta meta;
    std::string error;
    ui::Dialog dialog;
    dialog.header = text::lower(filetype::label(kind));
    if (!titlemeta::read(path, kind, meta, error)) {
        dialog.title = basename(path);
        dialog.tone = ui::Tone::Warning;
        dialog.body.push_back({error, ui::color::Warning, 0.48f, 3});
    } else {
        dialog.title = meta.name.empty() ? basename(path) : meta.name;
        dialog.icon = ui::showIcon(meta.icon);
        if (!meta.publisher.empty()) {
            dialog.body.push_back({meta.publisher, ui::color::Muted, 0.46f,
                                   1});
        }
        std::string facts = filetype::label(kind);
        if (meta.hasTitleId) {
            facts += ", " + cia::titleIdText(meta.titleId);
        }
        if (!meta.productCode.empty()) {
            facts += ", " + meta.productCode;
        }
        dialog.body.push_back({facts, ui::color::Text, 0.46f, 2});
        dialog.body.push_back({sizeText(fs::fileSize(path)), ui::color::Text,
                               0.46f, 1});
        if (!meta.note.empty()) {
            dialog.body.push_back({meta.note, ui::color::Muted, 0.44f, 2});
        }
        if (kind == Kind::Homebrew) {
            dialog.body.push_back({"Start it from the Homebrew Launcher.",
                                   ui::color::Muted, 0.44f, 1});
        }
    }
    dialog.buttons = {"OPEN AS HEX", "CLOSE"};
    dialog.initial = 1;
    dialog.cancelButton = 1;
    if (ui::choose(dialog) == 0) {
        hexViewer(path);
    }
}

void App::pictureViewer(const std::vector<std::string> &pictures,
                        size_t index)
{
    if (pictures.empty()) {
        return;
    }
    const ui::Rect previous = {12, 150, 96, 40};
    const ui::Rect next = {112, 150, 96, 40};
    const ui::Rect close = {212, 150, 96, 40};

    while (ui::keepRunning()) {
        const std::string &path = pictures[index];
        picture::Picture shown;
        std::string error;
        tasks::busy("picture", "Opening " + basename(path) + "...", [&]() {
            picture::load(path, 400, 240, PictureBackground, shown, error);
        });

        // Uploaded once; drawing it again every frame is cheap.
        const C2D_Image *image =
            error.empty()
                ? ui::showPicture(shown.pixels, shown.width, shown.height)
                : nullptr;

        bool moved = false;
        while (ui::keepRunning() && !moved) {
            const ui::Input input = ui::readInput();
            if ((input.down & (KEY_B | KEY_A)) || input.tapped(close)) {
                return;
            }
            if (pictures.size() > 1) {
                if ((input.down & (KEY_LEFT | KEY_L)) ||
                    input.tapped(previous)) {
                    index = (index + pictures.size() - 1) % pictures.size();
                    moved = true;
                }
                if ((input.down & (KEY_RIGHT | KEY_R)) || input.tapped(next)) {
                    index = (index + 1) % pictures.size();
                    moved = true;
                }
            }

            ui::beginFrame();
            C2D_DrawRectSolid(0, 0, 0.1f, ui::TopWidth, ui::ScreenHeight,
                              C2D_Color32(0x08, 0x10, 0x18, 0xff));
            if (image != nullptr) {
                C2D_DrawImageAt(*image, (ui::TopWidth - shown.width) / 2,
                                (ui::ScreenHeight - shown.height) / 2, 0.5f);
            } else {
                ui::drawWrapped(error.empty() ? "The picture could not be "
                                                "shown."
                                              : error,
                                34, 100, 0.5f, ui::color::Warning, 330, 4);
            }

            ui::beginBottom();
            ui::drawText(ui::fitMiddle(basename(path), 0.56f, 296), 12, 12,
                         0.56f, ui::color::Text);
            std::string facts = sizeText(fs::fileSize(path));
            if (shown.sourceWidth > 0) {
                facts = std::to_string(shown.sourceWidth) + " x " +
                        std::to_string(shown.sourceHeight) + ", " + facts;
            }
            ui::drawText(facts, 12, 38, 0.46f, ui::color::Muted);
            if (pictures.size() > 1) {
                ui::drawText(std::to_string(index + 1) + " of " +
                                 std::to_string(pictures.size()) +
                                 " pictures in this folder",
                             12, 58, 0.46f, ui::color::Muted);
                ui::drawButton(previous, "PREVIOUS", false);
                ui::drawButton(next, "NEXT", false);
            }
            ui::drawButton(close, "CLOSE", false);
            ui::drawText(pictures.size() > 1
                             ? "L/R or Left/Right: Browse    B: Close"
                             : "B: Close",
                         160, 214, 0.44f, ui::color::Muted, C2D_AlignCenter);
            ui::endFrame();
        }
    }
}

void App::zipViewer(const std::string &path)
{
    zip::Archive archive;
    std::string error;
    bool ok = false;
    tasks::busy("zip", "Reading " + basename(path) + "...",
                [&]() { ok = zip::readArchive(path, archive, error); });
    if (!ok) {
        ui::message("Cannot open the archive", error, ui::Tone::Error,
                    basename(path));
        return;
    }

    int cursor = 0;
    while (ui::keepRunning()) {
        ui::ListSpec spec;
        spec.header = "zip";
        spec.title = basename(path);
        spec.emptyText = "This archive is empty.";
        spec.extraKeys = KEY_X;
        spec.buttons = {{"BACK", KEY_B}, {"EXTRACT", KEY_X}};
        for (const zip::Entry &entry : archive.entries) {
            spec.items.push_back(
                {entry.name,
                 entry.directory ? "" : units::bytes(entry.size),
                 entry.path.empty() ? ui::color::Danger : ui::color::Text});
        }
        spec.drawTop = [&](int index) {
            drawLabelledPanel(56, 74, "ARCHIVE",
                              plural(archive.fileCount, "file") + ", " +
                                  units::bytes(archive.totalSize) +
                                  " when extracted",
                              ui::color::Text);
            if (index >= 0) {
                const zip::Entry &entry = archive.entries[index];
                drawLabelledPanel(
                    144, 76, entry.directory ? "FOLDER" : "FILE",
                    entry.path.empty()
                        ? entry.name + " (skipped: its path is unsafe)"
                        : entry.name,
                    ui::color::Text);
            }
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
    const int64_t fileSize = fs::fileSize(path);
    const uint64_t size = fileSize > 0 ? static_cast<uint64_t>(fileSize) : 0;
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ui::message("Cannot open the file", path, ui::Tone::Error);
        return;
    }

    const ui::Rect pageUp = {12, 120, 70, 36};
    const ui::Rect pageDown = {88, 120, 70, 36};
    const ui::Rect start = {164, 120, 70, 36};
    const ui::Rect end = {240, 120, 68, 36};
    const ui::Rect close = {58, 166, 204, 36};
    const uint64_t lastTop =
        size > Page ? ((size - Page + Columns - 1) / Columns) * Columns : 0;

    uint64_t top = 0;
    uint64_t loadedAt = UINT64_MAX;
    std::vector<uint8_t> bytes(Page);
    size_t count = 0;
    while (ui::keepRunning()) {
        const ui::Input input = ui::readInput();
        if ((input.down & KEY_B) || input.tapped(close)) {
            break;
        }
        if (input.repeat & KEY_DOWN) {
            top = std::min(lastTop, top + Columns);
        }
        if (input.repeat & KEY_UP) {
            top = top >= Columns ? top - Columns : 0;
        }
        if ((input.repeat & (KEY_RIGHT | KEY_R)) || input.tapped(pageDown)) {
            top = std::min(lastTop, top + Page);
        }
        if ((input.repeat & (KEY_LEFT | KEY_L)) || input.tapped(pageUp)) {
            top = top >= Page ? top - Page : 0;
        }
        if (input.tapped(start)) {
            top = 0;
        }
        if (input.tapped(end)) {
            top = lastTop;
        }
        if (loadedAt != top) {
            count = 0;
            if (fseeko(file, static_cast<off_t>(top), SEEK_SET) == 0) {
                count = std::fread(bytes.data(), 1, bytes.size(), file);
            }
            loadedAt = top;
        }

        ui::beginFrame();
        ui::drawHeader("hex");
        for (int row = 0; row < Rows; ++row) {
            const size_t first = static_cast<size_t>(row) * Columns;
            if (first >= count) {
                break;
            }
            const float y = 50 + row * 15;
            char offset[16];
            std::snprintf(offset, sizeof(offset), "%08llX",
                          static_cast<unsigned long long>(top + first));
            ui::drawText(offset, 14, y, 0.42f, ui::color::Muted);
            for (int column = 0; column < Columns; ++column) {
                if (first + column >= count) {
                    break;
                }
                const uint8_t value = bytes[first + column];
                char hex[4];
                std::snprintf(hex, sizeof(hex), "%02X", value);
                ui::drawText(hex, 92 + column * 25 + (column >= 4 ? 6 : 0), y,
                             0.42f, ui::color::Text);
                const char shown[2] = {
                    value >= 0x20 && value < 0x7f ? static_cast<char>(value)
                                                  : '.',
                    0};
                ui::drawText(shown, 306 + column * 10, y, 0.42f,
                             ui::color::Success);
            }
        }

        ui::beginBottom();
        ui::drawText(ui::fitMiddle(basename(path), 0.54f, 296), 12, 12, 0.54f,
                     ui::color::Text);
        char where[96];
        std::snprintf(where, sizeof(where), "Offset 0x%llX of %s",
                      static_cast<unsigned long long>(top),
                      units::bytes(size).c_str());
        ui::drawText(where, 12, 38, 0.46f, ui::color::Muted);
        ui::drawText("Up/Down: line    Left/Right, L/R: page", 12, 60, 0.44f,
                     ui::color::Muted);
        ui::drawButton(pageUp, "PAGE UP", false);
        ui::drawButton(pageDown, "PAGE DN", false);
        ui::drawButton(start, "START", false);
        ui::drawButton(end, "END", false);
        ui::drawButton(close, "CLOSE", false);
        ui::endFrame();
    }
    std::fclose(file);
}

void App::textEditor(const std::string &path)
{
    textdoc::Document document;
    switch (textdoc::load(path, document)) {
    case textdoc::Load::Unreadable:
        ui::message("Cannot open the file", path, ui::Tone::Error);
        return;
    case textdoc::Load::NotText: {
        ui::Dialog dialog;
        dialog.header = "text";
        dialog.title = basename(path) + " is not a text file";
        dialog.body.push_back({"It holds binary data. The hex view shows it "
                               "byte by byte.",
                               ui::color::Text, 0.48f, 3});
        dialog.buttons = {"OPEN AS HEX", "CLOSE"};
        dialog.cancelButton = 1;
        if (ui::choose(dialog) == 0) {
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

    const float lineHeight = ui::lineHeight(EditorScale);
    // Lines start below the header bar.
    constexpr float FirstLineY = 54;
    const int visible =
        static_cast<int>((ui::ScreenHeight - FirstLineY - 2) / lineHeight);
    int cursor = 0;
    int top = 0;
    bool dirty = false;
    bool saved = false; // shows "saved" until the next change

    std::vector<TouchButton> buttons = {
        {{12, 112, 96, 36}, "EDIT", !readOnly},
        {{112, 112, 96, 36}, "NEW LINE", !readOnly},
        {{212, 112, 96, 36}, "DELETE", !readOnly},
        {{12, 156, 70, 32}, "PAGE UP", true},
        {{88, 156, 70, 32}, "PAGE DN", true},
        {{164, 156, 70, 32}, "SAVE", !readOnly},
        {{240, 156, 68, 32}, "CLOSE", true},
    };
    enum { Edit, Insert, Delete, PageUp, PageDown, Save, Close };

    const auto save = [&]() {
        if (textdoc::save(path, document)) {
            dirty = false;
            saved = true;
            return true;
        }
        ui::message("Not saved", "The file could not be written. The SD card "
                                 "may be full or read-only.",
                    ui::Tone::Error, path);
        return false;
    };
    const auto editLine = [&](int index, bool inserting) {
        std::string typed;
        const std::string initial = inserting ? "" : document.lines[index];
        if (initial.size() > 1900) {
            ui::message("Line too long",
                        "This line is too long for the 3DS keyboard.",
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
        dirty = true;
        saved = false;
    };

    while (ui::keepRunning()) {
        const ui::Input input = ui::readInput();
        const int count = static_cast<int>(document.lines.size());
        int action = tappedButton(input, buttons);
        if (input.down & KEY_A) {
            action = Edit;
        } else if (input.down & KEY_X) {
            action = Insert;
        } else if (input.down & KEY_Y) {
            action = Delete;
        } else if (input.down & KEY_START) {
            action = Save;
        } else if (input.down & KEY_B) {
            action = Close;
        }
        if (input.repeat & KEY_DOWN) {
            cursor = std::min(count - 1, cursor + 1);
        }
        if (input.repeat & KEY_UP) {
            cursor = std::max(0, cursor - 1);
        }
        if ((input.repeat & (KEY_RIGHT | KEY_R)) || action == PageDown) {
            cursor = std::min(count - 1, cursor + visible);
        }
        if ((input.repeat & (KEY_LEFT | KEY_L)) || action == PageUp) {
            cursor = std::max(0, cursor - visible);
        }

        if (readOnly && (action == Edit || action == Insert ||
                         action == Delete || action == Save)) {
            ui::message("Read-only", readOnlyReason);
            action = -1;
        }
        switch (action) {
        case Edit:
            editLine(cursor, false);
            break;
        case Insert:
            editLine(cursor + 1, true);
            break;
        case Delete:
            if (document.lines.size() > 1) {
                document.lines.erase(document.lines.begin() + cursor);
                cursor = std::min(cursor,
                                  static_cast<int>(document.lines.size()) - 1);
            } else {
                document.lines[0].clear();
            }
            dirty = true;
            saved = false;
            break;
        case Save:
            save();
            break;
        case Close:
            if (dirty) {
                ui::Dialog dialog;
                dialog.header = "text";
                dialog.title = "Save the changes to " + basename(path) + "?";
                dialog.tone = ui::Tone::Warning;
                dialog.buttons = {"SAVE", "DISCARD", "KEEP EDITING"};
                dialog.dangerous = 1;
                dialog.cancelButton = 2;
                const int answer = ui::choose(dialog);
                if (answer == 0 && !save()) {
                    break;
                }
                if (answer == 2 || answer < 0) {
                    break;
                }
            }
            return;
        default:
            break;
        }

        if (cursor < top) {
            top = cursor;
        } else if (cursor >= top + visible) {
            top = cursor - visible + 1;
        }

        ui::beginFrame();
        ui::drawHeader(dirty      ? "edited"
                       : saved    ? "saved"
                       : readOnly ? "read-only"
                                  : "text");
        for (int row = 0; row < visible && top + row < static_cast<int>(
                                                          document.lines.size());
             ++row) {
            const int index = top + row;
            const float y = FirstLineY + row * lineHeight;
            if (index == cursor) {
                C2D_DrawRectSolid(0, y, 0.3f, ui::TopWidth, lineHeight,
                                  ui::color::SurfaceRaised);
            }
            ui::drawText(std::to_string(index + 1), 40, y, EditorScale,
                         ui::color::Muted, C2D_AlignRight);
            const std::string &line = document.lines[index];
            ui::drawText(ui::fitEnd(line, EditorScale, 344), 48, y,
                         EditorScale, lineColor(line, extension));
        }

        ui::beginBottom();
        ui::drawText(ui::fitMiddle(basename(path), 0.52f, 210), 12, 8, 0.52f,
                     ui::color::Text);
        ui::drawText("Line " + std::to_string(cursor + 1) + " of " +
                         std::to_string(document.lines.size()),
                     308, 10, 0.44f, ui::color::Muted, C2D_AlignRight);
        ui::drawPanel({12, 30, 296, 74});
        ui::drawWrapped(document.lines[cursor].empty()
                            ? "(empty line)"
                            : document.lines[cursor],
                        20, 36, 0.46f,
                        document.lines[cursor].empty() ? ui::color::Muted
                                                       : ui::color::Text,
                        280, 4);
        drawTouchButtons(buttons);
        ui::drawText("A: Edit  X: New line  Y: Delete  START: Save  B: Close",
                     160, 202, 0.38f, ui::color::Muted, C2D_AlignCenter);
        ui::drawText("Up/Down: line    Left/Right, L/R: page", 160, 220, 0.38f,
                     ui::color::Muted, C2D_AlignCenter);
        ui::endFrame();
    }
}
