#pragma once

#include "cia.hpp"
#include "ui.hpp"

#include <cstdint>
#include <string>

// What every request 3dsDL makes sends and trusts.
namespace http {
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
} // namespace http

// Small pieces of screen code shared by app.cpp and files.cpp.
namespace screens {

std::string basename(const std::string &path);
std::string plural(uint64_t count, const std::string &word);

// A panel on the top screen with a small label above a wrapped value.
void drawLabelledPanel(float y, float height, const std::string &label,
                       const std::string &value, u32 valueColor);

// The title's name from its icon data, or its title ID when it has none.
std::string ciaName(const cia::Info &info);
// Publisher, kind, version, size and the installed version, if any.
void addCiaParagraphs(ui::Dialog &dialog, const cia::Info &info);

// The top screen for a CIA: the title row, then its icon, name, publisher
// and Title ID, Version and Size tags.
void drawCiaTop(const cia::Info &info, const std::string &title,
                ui::Icon icon = ui::Icon::Package,
                u32 iconColor = ui::color::IconBlue);
// One line about the installed copy: "Not installed yet", "An update for
// the installed version", and so on.
// The Version row: "1.2.0, installed 1.1.0".
// A title's version as people read it: cia::versionText with this build of
// 3dsDL named exactly.
std::string titleVersion(uint64_t titleId, uint16_t version);
std::string versionRow(uint64_t titleId, uint16_t version, bool isInstalled,
                       uint16_t installed);
std::string installState(const cia::Info &info);

} // namespace screens
