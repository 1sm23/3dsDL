#include "app_util.hpp"

#include "installer.hpp"
#include "units.hpp"

namespace screens {

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

} // namespace screens
