#pragma once

#include "smdh.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Reads what 3dsDL shows about a CIA installer before installing it: the
// title ID and version from its TMD, the name and icon from its meta section
// and whether the game needs a seed. Installing is the AM service's job (see
// app/installer); this file has no libctru dependency so the tests can run it.
namespace cia {

constexpr size_t HeaderSize = 0x2020;

// Where each section of the file starts. Sections follow each other in this
// order, each aligned to 64 bytes.
struct Layout {
    uint32_t certSize = 0;
    uint32_t ticketSize = 0;
    uint32_t tmdSize = 0;
    uint32_t metaSize = 0;
    uint64_t contentSize = 0;
    uint64_t certOffset = 0;
    uint64_t ticketOffset = 0;
    uint64_t tmdOffset = 0;
    uint64_t contentOffset = 0;
    uint64_t metaOffset = 0;
    uint64_t end = 0;
};

// True when the first bytes of a file look like a CIA header. size must be
// at least 0x10 to say yes.
bool looksLikeCia(const uint8_t *data, size_t size);
bool parseLayout(const uint8_t *header, size_t size, Layout &layout);

enum class Seed { NotNeeded, Needed, Unknown };

struct Info {
    uint64_t titleId = 0;
    uint16_t version = 0;
    uint64_t fileSize = 0;
    bool hasSmdh = false;
    smdh::Info smdh;
    // Games from 2015 on can be encrypted with a seed the console has to
    // fetch. Unknown when the content is encrypted and its header unreadable.
    Seed seed = Seed::Unknown;
};

// Reads size bytes at offset into buffer. False when that is not possible.
using Reader = std::function<bool(uint64_t offset, void *buffer, size_t size)>;

// fileSize may be 0 when unknown. Sets error and returns false when the data
// is not a CIA.
bool readInfo(const Reader &read, uint64_t fileSize, Info &info,
              std::string &error);
bool readInfo(const std::string &path, Info &info, std::string &error);
// Checks only the header, for listing files quickly.
bool isCiaFile(const std::string &path);

// Reads a CIA as it arrives, for installing it without saving the file.
// The title ID and version are known once the header and title metadata
// have come in; the name and icon once the meta section at the end has.
class Stream {
public:
    // Hand over the data in order, from offset 0. Returns false, with a
    // reason, when it is not a CIA or arrives out of order.
    bool feed(uint64_t offset, const uint8_t *data, size_t size,
              std::string &error);
    // The title ID and version are known.
    bool ready() const { return ready_; }
    // Everything fed until ready, to give to the installer first. Empty
    // afterwards.
    std::vector<uint8_t> takePrefix();
    // Title ID, version and seed flag once ready; the name and icon too
    // once the meta section has passed.
    const Info &info() const { return info_; }

private:
    std::vector<uint8_t> prefix_;
    std::vector<uint8_t> meta_;
    Layout layout_;
    bool haveLayout_ = false;
    uint64_t prefixNeeded_ = 0;
    bool ready_ = false;
    uint64_t next_ = 0;
    uint64_t metaStart_ = 0;
    size_t metaGot_ = 0;
    Info info_;
};

// --- Title IDs ---------------------------------------------------------------

std::string titleIdText(uint64_t titleId); // 16 upper-case hex digits
// "1.2.0 (v1056)"
std::string versionText(uint16_t version);

enum class Kind { Application, Update, Dlc, Demo, DsiWare, DownloadPlay, System };
Kind kind(uint64_t titleId);
const char *kindName(Kind kind);

// System titles can brick a console when they go wrong, so 3dsDL does not
// install them.
bool isSystemTitle(uint64_t titleId);
// DSiWare, download play children and a few others belong in NAND, the way
// FBI decides it.
bool installsToNand(uint64_t titleId);
bool isNew3dsOnly(uint64_t titleId);
// The game an update or DLC belongs to, or titleId itself.
uint64_t baseTitle(uint64_t titleId);

} // namespace cia
