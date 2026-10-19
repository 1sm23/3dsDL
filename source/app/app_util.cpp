#include "app_util.hpp"

#include "installer.hpp"
#include "units.hpp"

#include <map>

namespace screens {
namespace {

// Asking AM is an IPC round trip, too slow for every frame, so answers are
// kept for a second.
bool installedCached(uint64_t titleId, uint16_t &version)
{
    struct Answer {
        uint64_t at;
        bool installed;
        uint16_t version;
    };
    static std::map<uint64_t, Answer> answers;
    const uint64_t now = osGetTime();
    auto found = answers.find(titleId);
    if (found == answers.end() || now - found->second.at > 1000) {
        Answer answer {now, false, 0};
        answer.installed = installer::installedVersion(titleId, answer.version);
        found = answers.insert_or_assign(titleId, answer).first;
    }
    version = found->second.version;
    return found->second.installed;
}

} // namespace

std::string basename(const std::string &path)
{
    return path.substr(path.find_last_of('/') + 1);
}

std::string plural(uint64_t count, const std::string &word)
{
    return std::to_string(count) + " " + word + (count == 1 ? "" : "s");
}

void drawLabelledPanel(float y, float height, const std::string &label,
                       const std::string &value, u32 valueColor)
{
    ui::drawPanel({18, y, 364, height});
    ui::drawText(label, 34, y + 9, 0.44f, ui::color::Muted);
    ui::drawWrapped(value, 34, y + 27, 0.52f, valueColor, 330,
                    static_cast<size_t>((height - 30) / ui::lineHeight(0.52f)));
}

std::string ciaName(const cia::Info &info)
{
    return info.smdh.shortName.empty()
        ? "title " + cia::titleIdText(info.titleId)
        : info.smdh.shortName;
}

void addCiaParagraphs(ui::Dialog &dialog, const cia::Info &info)
{
    if (!info.smdh.publisher.empty()) {
        dialog.body.push_back({info.smdh.publisher, ui::color::Muted, 0.46f,
                               1});
    }
    dialog.body.push_back(
        {std::string(cia::kindName(cia::kind(info.titleId))) + ", " +
             cia::versionText(info.version) + ", " +
             units::bytes(info.fileSize),
         ui::color::Text, 0.46f, 2});
    uint16_t installed = 0;
    if (installer::installedVersion(info.titleId, installed)) {
        dialog.body.push_back({"Installed now: " +
                                   cia::versionText(installed),
                               installed > info.version ? ui::color::Warning
                                                        : ui::color::Muted,
                               0.44f, 1});
    }
}

void drawCiaTop(const cia::Info &info, const std::string &title,
                ui::Icon icon, u32 iconColor)
{
    ui::drawTopChrome(title, icon, iconColor);
    ui::drawPanel();
    const float left = ui::TopPanel.x + 18;
    const float top = ui::TopPanel.y + 12;
    ui::drawTitleIcon(info.smdh.icon, left, top);
    ui::drawText(ui::fitEnd(ciaName(info), 0.78f, 280), left + 60, top + 1,
                 0.78f, ui::color::InkStrong);
    ui::drawText(ui::fitEnd(info.smdh.publisher.empty()
                                ? cia::kindName(cia::kind(info.titleId))
                                : info.smdh.publisher,
                            ui::type::Small, 280),
                 left + 60, top + 30, ui::type::Small, ui::color::InkSoft);
    float y = top + 60;
    y = ui::drawTagRow(left, y, 76, "Title ID", cia::titleIdText(info.titleId));
    uint16_t installed = 0;
    const bool isInstalled = installedCached(info.titleId, installed);
    y = ui::drawTagRow(left, y, 76, "Version",
                       cia::versionText(info.version) +
                           (isInstalled ? ", installed: " +
                                              cia::versionText(installed)
                                        : ""));
    ui::drawTagRow(left, y, 76, "Size",
                   units::bytes(info.fileSize) + ", " +
                       (cia::installsToNand(info.titleId)
                            ? "installs to the system memory"
                            : "installs to the SD card"));
}

std::string installState(const cia::Info &info)
{
    uint16_t installed = 0;
    if (!installer::installedVersion(info.titleId, installed)) {
        return "Not installed yet.";
    }
    if (installed < info.version) {
        return "An update for the installed version.";
    }
    if (installed > info.version) {
        return "Older than the installed version.";
    }
    return "This version is already installed.";
}

} // namespace screens
