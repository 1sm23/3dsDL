#include "cia.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace cia {
namespace {

constexpr uint32_t CertificateChainSize = 0xa00;
constexpr uint64_t Alignment = 64;
// The meta section starts with dependencies and the core version; the SMDH
// follows.
constexpr uint64_t SmdhInMeta = 0x400;
constexpr size_t TmdHeaderSize = 0xc4;
constexpr size_t ContentInfoRecordsSize = 0x900;
constexpr size_t ContentChunkSize = 0x30;
constexpr uint16_t ContentEncrypted = 0x0001;
constexpr size_t NcchHeaderSize = 0x200;
constexpr uint8_t NcchNoCrypto = 0x04;
constexpr uint8_t NcchUsesSeed = 0x20;

uint16_t le16(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t le32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t le64(const uint8_t *data)
{
    return le32(data) | (static_cast<uint64_t>(le32(data + 4)) << 32);
}

uint16_t be16(const uint8_t *data)
{
    return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

uint32_t be32(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

uint64_t be64(const uint8_t *data)
{
    return (static_cast<uint64_t>(be32(data)) << 32) | be32(data + 4);
}

uint64_t align(uint64_t value)
{
    return (value + Alignment - 1) & ~(Alignment - 1);
}

// Bytes taken by the signature type, the signature and its padding at the
// start of a TMD, or 0 for an unknown type.
size_t signatureBlockSize(uint32_t type)
{
    switch (type) {
    case 0x10000: // RSA-4096 with SHA-1
    case 0x10003: // RSA-4096 with SHA-256
        return 4 + 0x200 + 0x3c;
    case 0x10001: // RSA-2048 with SHA-1
    case 0x10004: // RSA-2048 with SHA-256, what 3DS TMDs use
        return 4 + 0x100 + 0x3c;
    case 0x10002: // ECDSA with SHA-1
    case 0x10005: // ECDSA with SHA-256
        return 4 + 0x3c + 0x40;
    default:
        return 0;
    }
}

uint16_t titleCategory(uint64_t titleId)
{
    return static_cast<uint16_t>(titleId >> 32);
}

uint16_t titlePlatform(uint64_t titleId)
{
    return static_cast<uint16_t>(titleId >> 48);
}

} // namespace

bool looksLikeCia(const uint8_t *data, size_t size)
{
    return size >= 0x10 && le32(data) == HeaderSize && le16(data + 4) == 0 &&
           le32(data + 8) == CertificateChainSize;
}

bool parseLayout(const uint8_t *header, size_t size, Layout &layout)
{
    if (size < 0x20 || !looksLikeCia(header, size)) {
        return false;
    }
    layout = Layout();
    layout.certSize = le32(header + 0x08);
    layout.ticketSize = le32(header + 0x0c);
    layout.tmdSize = le32(header + 0x10);
    layout.metaSize = le32(header + 0x14);
    layout.contentSize = le64(header + 0x18);
    // A content section over 64 GiB is not a real CIA and would overflow
    // the offsets below.
    if (layout.tmdSize == 0 || layout.contentSize > (1ULL << 36)) {
        return false;
    }
    layout.certOffset = align(HeaderSize);
    layout.ticketOffset = align(layout.certOffset + layout.certSize);
    layout.tmdOffset = align(layout.ticketOffset + layout.ticketSize);
    layout.contentOffset = align(layout.tmdOffset + layout.tmdSize);
    layout.metaOffset = align(layout.contentOffset + layout.contentSize);
    layout.end = layout.metaOffset + layout.metaSize;
    return true;
}

bool readInfo(const Reader &read, uint64_t fileSize, Info &info,
              std::string &error)
{
    info = Info();
    info.fileSize = fileSize;

    uint8_t header[0x20];
    Layout layout;
    if (!read(0, header, sizeof(header)) ||
        !parseLayout(header, sizeof(header), layout)) {
        error = "This is not a CIA file.";
        return false;
    }
    if (fileSize > 0 && layout.metaOffset > fileSize) {
        error = "The CIA file is incomplete. It may not have finished "
                "downloading.";
        return false;
    }

    uint8_t type[4];
    if (!read(layout.tmdOffset, type, sizeof(type))) {
        error = "Could not read the CIA's title metadata.";
        return false;
    }
    const size_t signature = signatureBlockSize(be32(type));
    const size_t chunkOffset = signature + TmdHeaderSize +
                               ContentInfoRecordsSize;
    if (signature == 0 || chunkOffset + ContentChunkSize > layout.tmdSize) {
        error = "The CIA's title metadata is damaged.";
        return false;
    }
    std::vector<uint8_t> tmd(chunkOffset + ContentChunkSize);
    if (!read(layout.tmdOffset, tmd.data(), tmd.size())) {
        error = "Could not read the CIA's title metadata.";
        return false;
    }
    const uint8_t *tmdHeader = tmd.data() + signature;
    info.titleId = be64(tmdHeader + 0x4c);
    info.version = be16(tmdHeader + 0x9c);
    const uint16_t contentCount = be16(tmdHeader + 0x9e);

    // The seed flag lives in the header of the first content, which can only
    // be read when the CIA was made without title key encryption.
    const uint8_t *chunk = tmd.data() + chunkOffset;
    if (contentCount > 0 && (be16(chunk + 6) & ContentEncrypted) == 0) {
        uint8_t ncch[NcchHeaderSize];
        if (read(layout.contentOffset, ncch, sizeof(ncch)) &&
            std::memcmp(ncch + 0x100, "NCCH", 4) == 0) {
            const uint8_t flags = ncch[0x18f];
            info.seed = (flags & NcchNoCrypto) == 0 && (flags & NcchUsesSeed)
                ? Seed::Needed
                : Seed::NotNeeded;
        }
    }

    if (layout.metaSize >= SmdhInMeta + smdh::Size) {
        std::vector<uint8_t> block(smdh::Size);
        if (read(layout.metaOffset + SmdhInMeta, block.data(), block.size())) {
            info.hasSmdh = smdh::parse(block.data(), block.size(), info.smdh);
        }
    }
    return true;
}

bool readInfo(const std::string &path, Info &info, std::string &error)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        info = Info();
        error = "Could not open the file.";
        return false;
    }
    uint64_t size = 0;
    if (fseeko(file, 0, SEEK_END) == 0) {
        const off_t end = ftello(file);
        size = end > 0 ? static_cast<uint64_t>(end) : 0;
    }
    const Reader reader = [file](uint64_t offset, void *buffer, size_t count) {
        return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0 &&
               std::fread(buffer, 1, count, file) == count;
    };
    const bool ok = readInfo(reader, size, info, error);
    std::fclose(file);
    return ok;
}

bool isCiaFile(const std::string &path)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    uint8_t header[0x10];
    const bool ok = std::fread(header, 1, sizeof(header), file) ==
                        sizeof(header) &&
                    looksLikeCia(header, sizeof(header));
    std::fclose(file);
    return ok;
}

std::string titleIdText(uint64_t titleId)
{
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llX",
                  static_cast<unsigned long long>(titleId));
    return buffer;
}

std::string versionText(uint16_t version)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%u.%u.%u (v%u)",
                  static_cast<unsigned>((version >> 10) & 0x3f),
                  static_cast<unsigned>((version >> 4) & 0x3f),
                  static_cast<unsigned>(version & 0xf),
                  static_cast<unsigned>(version));
    return buffer;
}

Kind kind(uint64_t titleId)
{
    const uint16_t category = titleCategory(titleId);
    if (isSystemTitle(titleId)) {
        return Kind::System;
    }
    if (titlePlatform(titleId) == 0x0003 || (category & 0x8000) != 0) {
        return Kind::DsiWare;
    }
    switch (category) {
    case 0x0001:
        return Kind::DownloadPlay;
    case 0x0002:
        return Kind::Demo;
    case 0x000e:
        return Kind::Update;
    case 0x008c:
        return Kind::Dlc;
    default:
        return Kind::Application;
    }
}

const char *kindName(Kind value)
{
    switch (value) {
    case Kind::Application:
        return "Game or app";
    case Kind::Update:
        return "Update";
    case Kind::Dlc:
        return "DLC";
    case Kind::Demo:
        return "Demo";
    case Kind::DsiWare:
        return "DSiWare";
    case Kind::DownloadPlay:
        return "Download Play";
    case Kind::System:
        break;
    }
    return "System title";
}

bool isSystemTitle(uint64_t titleId)
{
    return titlePlatform(titleId) == 0x0004 &&
           (titleCategory(titleId) & 0x0010) != 0;
}

bool installsToNand(uint64_t titleId)
{
    const uint16_t platform = titlePlatform(titleId);
    const uint16_t category = titleCategory(titleId);
    const uint8_t variation = static_cast<uint8_t>(titleId);
    return platform == 0x0003 ||
           (platform == 0x0004 &&
            ((category & 0x8011) != 0 ||
             (category == 0x0000 && variation == 0x02)));
}

bool isNew3dsOnly(uint64_t titleId)
{
    return ((titleId >> 28) & 0xf) == 2;
}

uint64_t baseTitle(uint64_t titleId)
{
    const Kind value = kind(titleId);
    if (value == Kind::Update || value == Kind::Dlc) {
        // Same platform and unique ID, application category.
        return titleId & 0xffff0000ffffffffULL;
    }
    return titleId;
}

} // namespace cia
