#include "audio_meta.hpp"

#include "text.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace audiometa {
namespace {

// No real tag or header comes near this; past it the file is not read on,
// since a 3DS app has little memory.
constexpr uint64_t MaxTag = 16 * 1024 * 1024;
// How far past the tags to look for the first MPEG frame, and how much of
// the end of an Ogg file holds its last page.
constexpr uint64_t SearchWindow = 64 * 1024;

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

uint32_t be24(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) | data[2];
}

uint32_t be32(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0]) << 24) | be24(data + 1);
}

uint64_t be64(const uint8_t *data)
{
    return (static_cast<uint64_t>(be32(data)) << 32) | be32(data + 4);
}

// ID3v2 sizes keep the top bit of every byte clear.
uint32_t syncsafe(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0] & 0x7f) << 21) |
           (static_cast<uint32_t>(data[1] & 0x7f) << 14) |
           (static_cast<uint32_t>(data[2] & 0x7f) << 7) | (data[3] & 0x7f);
}

// Up to count bytes at offset: fewer at the end of the file, none on error.
std::vector<uint8_t> readUpTo(const cia::Reader &read, uint64_t size,
                              uint64_t offset, uint64_t count)
{
    std::vector<uint8_t> bytes;
    if (offset >= size) {
        return bytes;
    }
    bytes.resize(static_cast<size_t>(std::min(count, size - offset)));
    if (!bytes.empty() && !read(offset, bytes.data(), bytes.size())) {
        bytes.clear();
    }
    return bytes;
}

// A tag value as one line of valid UTF-8.
std::string clean(const std::string &value)
{
    return text::trim(text::singleLine(text::sanitizeUtf8(value)));
}

void appendUtf8(std::string &out, uint32_t code)
{
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xc0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3f));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xe0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (code & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (code & 0x3f));
    }
}

// An ID3v2 string up to its terminator, in encoding 0 (Latin-1), 1 (UTF-16
// with a byte order mark), 2 (UTF-16BE) or 3 (UTF-8). used is set to the
// bytes taken, terminator included.
std::string id3Text(const uint8_t *data, size_t size, int encoding,
                    size_t *used = nullptr)
{
    std::string out;
    size_t index = 0;
    if (encoding == 0 || encoding == 3) {
        for (; index < size && data[index] != 0; ++index) {
            if (encoding == 0) {
                appendUtf8(out, data[index]);
            } else {
                out += static_cast<char>(data[index]);
            }
        }
        index = std::min(index + 1, size);
    } else {
        bool bigEndian = encoding == 2;
        if (encoding == 1 && size >= 2) {
            if (data[0] == 0xff && data[1] == 0xfe) {
                index = 2;
            } else if (data[0] == 0xfe && data[1] == 0xff) {
                index = 2;
                bigEndian = true;
            }
        }
        uint32_t high = 0;
        for (; index + 1 < size; index += 2) {
            const uint32_t unit =
                bigEndian ? be16(data + index) : le16(data + index);
            if (unit == 0) {
                index += 2;
                break;
            }
            if (unit >= 0xd800 && unit < 0xdc00) {
                high = unit;
            } else if (unit >= 0xdc00 && unit < 0xe000) {
                if (high != 0) {
                    appendUtf8(out, 0x10000 + ((high - 0xd800) << 10) +
                                        (unit - 0xdc00));
                }
                high = 0;
            } else {
                appendUtf8(out, unit);
            }
        }
        index = std::min(index, size);
    }
    if (used != nullptr) {
        *used = index;
    }
    return clean(out);
}

std::string latin1(const uint8_t *data, size_t size)
{
    return id3Text(data, size, 0);
}

int leadingNumber(const std::string &value, size_t from = 0)
{
    int number = 0;
    size_t index = from;
    for (; index < value.size() && value[index] >= '0' && value[index] <= '9' &&
           number < 100000;
         ++index) {
        number = number * 10 + (value[index] - '0');
    }
    return number;
}

// "3/12" into 3 and 12.
void takeCount(const std::string &value, int &number, int &total)
{
    number = leadingNumber(value);
    const size_t slash = value.find('/');
    if (slash != std::string::npos) {
        total = leadingNumber(value, slash + 1);
    }
}

std::string yearOf(const std::string &value)
{
    // "2001", "2001-05-14" and "2001-05-14T00:00:00Z" all start with it.
    if (value.size() >= 4 &&
        std::all_of(value.begin(), value.begin() + 4,
                    [](char c) { return c >= '0' && c <= '9'; })) {
        return value.substr(0, 4);
    }
    return value;
}

// The ID3v1 genres with Winamp's additions, which ID3v2 and MP4 files refer
// to by number.
const char *const Genres[] = {
    "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge",
    "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B",
    "Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska",
    "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient",
    "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical",
    "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
    "Alternative Rock", "Bass", "Soul", "Punk", "Space", "Meditative",
    "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic", "Darkwave",
    "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream",
    "Southern Rock", "Comedy", "Cult", "Gangsta", "Top 40", "Christian Rap",
    "Pop/Funk", "Jungle", "Native American", "Cabaret", "New Wave",
    "Psychedelic", "Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal",
    "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll",
    "Hard Rock", "Folk", "Folk-Rock", "National Folk", "Swing",
    "Fast Fusion", "Bebop", "Latin", "Revival", "Celtic", "Bluegrass",
    "Avantgarde", "Gothic Rock", "Progressive Rock", "Psychedelic Rock",
    "Symphonic Rock", "Slow Rock", "Big Band", "Chorus", "Easy Listening",
    "Acoustic", "Humour", "Speech", "Chanson", "Opera", "Chamber Music",
    "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove", "Satire",
    "Slow Jam", "Club", "Tango", "Samba", "Folklore", "Ballad",
    "Power Ballad", "Rhythmic Soul", "Freestyle", "Duet", "Punk Rock",
    "Drum Solo", "A Cappella", "Euro-House", "Dance Hall", "Goa",
    "Drum & Bass", "Club-House", "Hardcore", "Terror", "Indie", "BritPop",
    "Afro-Punk", "Polsk Punk", "Beat", "Christian Gangsta Rap",
    "Heavy Metal", "Black Metal", "Crossover", "Contemporary Christian",
    "Christian Rock", "Merengue", "Salsa", "Thrash Metal", "Anime", "JPop",
    "Synthpop", "Abstract", "Art Rock", "Baroque", "Bhangra", "Big Beat",
    "Breakbeat", "Chillout", "Downtempo", "Dub", "EBM", "Eclectic",
    "Electro", "Electroclash", "Emo", "Experimental", "Garage", "Global",
    "IDM", "Illbient", "Industro-Goth", "Jam Band", "Krautrock", "Leftfield",
    "Lounge", "Math Rock", "New Romantic", "Nu-Breakz", "Post-Punk",
    "Post-Rock", "Psytrance", "Shoegaze", "Space Rock", "Trop Rock",
    "World Music", "Neoclassical", "Audiobook", "Audio Theatre",
    "Neue Deutsche Welle", "Podcast", "Indie Rock", "G-Funk", "Dubstep",
    "Garage Rock", "Psybient"};

std::string genreNumber(int number)
{
    const int count = static_cast<int>(sizeof(Genres) / sizeof(Genres[0]));
    return number >= 0 && number < count ? Genres[number] : "";
}

// An ID3v2 genre: a name, a number, or "(17)" with or without a name after.
std::string id3Genre(const std::string &value)
{
    if (value.size() > 2 && value[0] == '(' && value[1] >= '0' &&
        value[1] <= '9') {
        const size_t close = value.find(')');
        if (close != std::string::npos) {
            const std::string rest = text::trim(value.substr(close + 1));
            return rest.empty() ? genreNumber(leadingNumber(value, 1)) : rest;
        }
    }
    if (!value.empty() &&
        std::all_of(value.begin(), value.end(),
                    [](char c) { return c >= '0' && c <= '9'; })) {
        return genreNumber(leadingNumber(value));
    }
    return value;
}

void keepCover(Info &info, std::vector<uint8_t> bytes)
{
    if (!bytes.empty() && bytes.size() <= MaxCover) {
        info.cover = std::move(bytes);
    }
}

// --- ID3 ---------------------------------------------------------------------

std::vector<uint8_t> deunsync(const std::vector<uint8_t> &bytes)
{
    std::vector<uint8_t> out;
    out.reserve(bytes.size());
    for (size_t index = 0; index < bytes.size(); ++index) {
        out.push_back(bytes[index]);
        if (bytes[index] == 0xff && index + 1 < bytes.size() &&
            bytes[index + 1] == 0) {
            ++index;
        }
    }
    return out;
}

// ID3v2.2's three-letter frames by their later names.
std::string frameName(const std::string &id)
{
    static const char *const Names[][2] = {
        {"TT2", "TIT2"}, {"TP1", "TPE1"}, {"TP2", "TPE2"}, {"TAL", "TALB"},
        {"TYE", "TYER"}, {"TCO", "TCON"}, {"TRK", "TRCK"}, {"TPA", "TPOS"},
        {"PIC", "APIC"}};
    if (id.size() == 3) {
        for (const auto &name : Names) {
            if (id == name[0]) {
                return name[1];
            }
        }
    }
    return id;
}

bool wantedText(const std::string &name)
{
    static const char *const Wanted[] = {"TIT2", "TPE1", "TPE2", "TALB",
                                         "TYER", "TDRC", "TCON", "TRCK",
                                         "TPOS"};
    for (const char *wanted : Wanted) {
        if (name == wanted) {
            return true;
        }
    }
    return false;
}

void takeId3Text(const std::string &name, const std::vector<uint8_t> &frame,
                 Info &info)
{
    if (frame.size() < 2) {
        return;
    }
    const std::string value = id3Text(frame.data() + 1, frame.size() - 1,
                                      frame[0]);
    if (value.empty()) {
        return;
    }
    if (name == "TIT2") {
        info.title = value;
    } else if (name == "TPE1") {
        info.artist = value;
    } else if (name == "TPE2") {
        info.albumArtist = value;
    } else if (name == "TALB") {
        info.album = value;
    } else if (name == "TYER" || name == "TDRC") {
        info.year = yearOf(value);
    } else if (name == "TCON") {
        info.genre = id3Genre(value);
    } else if (name == "TRCK") {
        takeCount(value, info.track, info.tracks);
    } else if (name == "TPOS") {
        takeCount(value, info.disc, info.discs);
    }
}

// An APIC frame (PIC in ID3v2.2). Returns true for a front cover.
bool takeId3Picture(const std::vector<uint8_t> &frame, int major, Info &info)
{
    const uint8_t *data = frame.data();
    const size_t size = frame.size();
    if (size < 6) {
        return false;
    }
    const int encoding = data[0];
    size_t index = 1;
    if (major == 2) {
        index += 3; // "JPG" or "PNG" in place of a MIME type
    } else {
        while (index < size && data[index] != 0) {
            ++index;
        }
        ++index;
    }
    if (index >= size) {
        return false;
    }
    const int type = data[index++];
    size_t used = 0;
    id3Text(data + index, size - index, encoding, &used);
    index += used;
    if (index >= size) {
        return false;
    }
    // The front cover wins; otherwise the first picture there is.
    if (info.cover.empty() || type == 3) {
        keepCover(info, std::vector<uint8_t>(frame.begin() + index,
                                             frame.end()));
    }
    return type == 3;
}

// Reads the ID3v2 tag at offset into info and returns its whole size, or 0
// when there is none. Frames are fetched one at a time, so a tag carrying a
// large cover costs nothing more when the cover is not wanted.
uint64_t readId3v2(const cia::Reader &read, uint64_t size, uint64_t offset,
                   Info &info, bool withCover)
{
    uint8_t header[10];
    if (offset + 10 > size || !read(offset, header, 10) ||
        std::memcmp(header, "ID3", 3) != 0) {
        return 0;
    }
    const int major = header[3];
    const uint8_t flags = header[5];
    const uint64_t body = syncsafe(header + 6);
    const uint64_t total = 10 + body + (major == 4 && (flags & 0x10) ? 10 : 0);
    // ID3v2.2 compression was never defined, so such tags are skipped.
    if (major < 2 || major > 4 || body > MaxTag ||
        (major == 2 && (flags & 0x40))) {
        return total;
    }
    const uint64_t start = offset + 10;
    // Before ID3v2.4, unsynchronisation covers the tag as a whole, across
    // frame boundaries, so that tag is read in one go.
    const bool wholeUnsync = major < 4 && (flags & 0x80);
    std::vector<uint8_t> whole;
    if (wholeUnsync) {
        whole = deunsync(readUpTo(read, size, start, body));
    }
    const uint64_t limit = wholeUnsync
                               ? whole.size()
                               : std::min(body, size > start ? size - start : 0);
    const auto fetch = [&](uint64_t at, uint64_t count) {
        count = std::min(count, limit - std::min(at, limit));
        if (!wholeUnsync) {
            return readUpTo(read, size, start + at, count);
        }
        const auto from = whole.begin() + static_cast<long>(at);
        return std::vector<uint8_t>(from, from + static_cast<long>(count));
    };

    uint64_t at = 0;
    if (major > 2 && (flags & 0x40)) {
        const std::vector<uint8_t> extended = fetch(0, 4);
        if (extended.size() < 4) {
            return total;
        }
        at = major == 4 ? syncsafe(extended.data())
                        : 4 + static_cast<uint64_t>(be32(extended.data()));
    }
    const uint64_t headerSize = major == 2 ? 6 : 10;
    bool haveFront = false;
    while (at + headerSize <= limit) {
        const std::vector<uint8_t> frameHeader = fetch(at, headerSize);
        if (frameHeader.size() < headerSize || frameHeader[0] == 0) {
            break; // padding
        }
        const std::string id(reinterpret_cast<const char *>(frameHeader.data()),
                             major == 2 ? 3 : 4);
        uint64_t frameSize = 0;
        uint16_t frameFlags = 0;
        if (major == 2) {
            frameSize = be24(frameHeader.data() + 3);
        } else {
            frameSize = major == 4 ? syncsafe(frameHeader.data() + 4)
                                   : be32(frameHeader.data() + 4);
            frameFlags = be16(frameHeader.data() + 8);
        }
        at += headerSize;
        if (frameSize == 0 || frameSize > limit - at) {
            break;
        }
        const uint64_t frameStart = at;
        at += frameSize;

        const std::string name = frameName(id);
        const bool picture = name == "APIC";
        if (picture ? !withCover || haveFront || frameSize > MaxCover + 1024
                    : !wantedText(name)) {
            continue;
        }
        // Compressed and encrypted frames are left alone. Grouping adds a
        // byte and ID3v2.4's data length indicator four before the data.
        size_t skip = 0;
        if (major == 3) {
            if (frameFlags & 0x00c0) {
                continue;
            }
            skip = (frameFlags & 0x0020) ? 1 : 0;
        } else if (major == 4) {
            if (frameFlags & 0x000c) {
                continue;
            }
            skip = ((frameFlags & 0x0040) ? 1 : 0) +
                   ((frameFlags & 0x0001) ? 4 : 0);
        }
        std::vector<uint8_t> frame = fetch(frameStart, frameSize);
        if (frame.size() <= skip) {
            continue;
        }
        frame.erase(frame.begin(), frame.begin() + static_cast<long>(skip));
        if (major == 4 && ((frameFlags & 0x0002) || (flags & 0x80))) {
            frame = deunsync(frame);
        }
        if (picture) {
            haveFront = takeId3Picture(frame, major, info);
        } else {
            takeId3Text(name, frame, info);
        }
    }
    return total;
}

// The 128-byte tag at the very end. Fills only what ID3v2 left empty and
// returns whether it is there.
bool readId3v1(const cia::Reader &read, uint64_t size, Info &info)
{
    uint8_t tag[128];
    if (size < 128 || !read(size - 128, tag, 128) ||
        std::memcmp(tag, "TAG", 3) != 0) {
        return false;
    }
    const auto fill = [](std::string &field, const std::string &value) {
        if (field.empty()) {
            field = value;
        }
    };
    fill(info.title, latin1(tag + 3, 30));
    fill(info.artist, latin1(tag + 33, 30));
    fill(info.album, latin1(tag + 63, 30));
    fill(info.year, latin1(tag + 93, 4));
    if (info.track == 0 && tag[125] == 0 && tag[126] != 0) {
        info.track = tag[126];
    }
    if (tag[127] != 0xff) {
        fill(info.genre, genreNumber(tag[127]));
    }
    return true;
}

// --- MP3 ---------------------------------------------------------------------

struct MpegFrame {
    bool mpeg1 = false;
    int layer = 0;
    uint32_t bitrate = 0; // bits per second
    uint32_t sampleRate = 0;
    bool mono = false;
    uint32_t length = 0;  // bytes, header included
    uint32_t samples = 0; // per channel
};

bool parseMpegHeader(const uint8_t *data, MpegFrame &frame)
{
    static const uint16_t Bitrates[2][3][16] = {
        {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
         {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
         {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}},
        {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
         {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
         {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}}};
    static const uint16_t Rates[4][3] = {{11025, 12000, 8000},
                                         {0, 0, 0},
                                         {22050, 24000, 16000},
                                         {44100, 48000, 32000}};
    if (data[0] != 0xff || (data[1] & 0xe0) != 0xe0) {
        return false;
    }
    const int version = (data[1] >> 3) & 3; // 0 MPEG-2.5, 2 MPEG-2, 3 MPEG-1
    const int layerBits = (data[1] >> 1) & 3;
    const int bitrateIndex = data[2] >> 4;
    const int rateIndex = (data[2] >> 2) & 3;
    if (version == 1 || layerBits == 0 || bitrateIndex == 0 ||
        bitrateIndex == 15 || rateIndex == 3) {
        return false;
    }
    frame.mpeg1 = version == 3;
    frame.layer = 4 - layerBits;
    frame.bitrate =
        Bitrates[frame.mpeg1 ? 0 : 1][frame.layer - 1][bitrateIndex] * 1000u;
    frame.sampleRate = Rates[version][rateIndex];
    frame.mono = (data[3] >> 6) == 3;
    const uint32_t padding = (data[2] >> 1) & 1;
    if (frame.layer == 1) {
        frame.samples = 384;
        frame.length = (12 * frame.bitrate / frame.sampleRate + padding) * 4;
    } else {
        frame.samples = frame.layer == 3 && !frame.mpeg1 ? 576 : 1152;
        frame.length = frame.samples / 8 * frame.bitrate / frame.sampleRate +
                       padding;
    }
    return frame.length > 4;
}

void readMpegAudio(const cia::Reader &read, uint64_t size, uint64_t audioStart,
                   uint64_t audioEnd, Info &info)
{
    const std::vector<uint8_t> buffer =
        readUpTo(read, size, audioStart, SearchWindow);
    for (size_t index = 0; index + 4 <= buffer.size(); ++index) {
        MpegFrame frame;
        if (!parseMpegHeader(buffer.data() + index, frame)) {
            continue;
        }
        // A real frame is followed by another, which stray bytes are not.
        MpegFrame next;
        const size_t nextAt = index + frame.length;
        if (nextAt + 4 <= buffer.size() &&
            !parseMpegHeader(buffer.data() + nextAt, next)) {
            continue;
        }
        info.codec = frame.layer == 3 ? "MP3" : frame.layer == 2 ? "MP2" : "MP1";
        info.sampleRate = frame.sampleRate;
        info.channels = frame.mono ? 1 : 2;
        const uint64_t frameStart = audioStart + index;
        uint64_t audioBytes = audioEnd > frameStart ? audioEnd - frameStart : 0;

        // A Xing or Info header in the first frame counts the frames of a
        // VBR file; Fraunhofer's encoder writes VBRI instead.
        uint64_t frames = 0;
        bool variable = false;
        const size_t sideInfo = frame.mpeg1 ? (frame.mono ? 17 : 32)
                                            : (frame.mono ? 9 : 17);
        const size_t xing = index + 4 + sideInfo;
        const size_t vbri = index + 4 + 32;
        if (xing + 16 <= buffer.size() &&
            (std::memcmp(buffer.data() + xing, "Xing", 4) == 0 ||
             std::memcmp(buffer.data() + xing, "Info", 4) == 0)) {
            variable = buffer[xing] == 'X';
            const uint32_t flags = be32(buffer.data() + xing + 4);
            size_t field = xing + 8;
            if (flags & 1) {
                frames = be32(buffer.data() + field);
                field += 4;
            }
            if ((flags & 2) && field + 4 <= buffer.size()) {
                audioBytes = be32(buffer.data() + field);
            }
        } else if (vbri + 18 <= buffer.size() &&
                   std::memcmp(buffer.data() + vbri, "VBRI", 4) == 0) {
            variable = true;
            audioBytes = be32(buffer.data() + vbri + 10);
            frames = be32(buffer.data() + vbri + 14);
        }
        if (frames > 0) {
            info.milliseconds = frames * frame.samples * 1000 / frame.sampleRate;
        } else {
            info.milliseconds = audioBytes * 8000 / frame.bitrate;
        }
        if (variable && info.milliseconds > 0) {
            info.kilobits = static_cast<uint32_t>(
                (audioBytes * 8 + info.milliseconds / 2) / info.milliseconds);
        } else {
            info.kilobits = frame.bitrate / 1000;
        }
        return;
    }
}

bool readMp3(const cia::Reader &read, uint64_t size, Info &info,
             bool withCover)
{
    const uint64_t tagSize = readId3v2(read, size, 0, info, withCover);
    const bool hasV1 = readId3v1(read, size, info);
    readMpegAudio(read, size, tagSize, size - (hasV1 ? 128 : 0), info);
    return !info.codec.empty() || tagSize > 0;
}

// --- Vorbis comments and FLAC pictures -----------------------------------------

std::vector<uint8_t> base64(const std::string &value)
{
    std::vector<uint8_t> out;
    out.reserve(value.size() * 3 / 4);
    uint32_t bits = 0;
    int count = 0;
    for (const char c : value) {
        int digit = -1;
        if (c >= 'A' && c <= 'Z') {
            digit = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            digit = c - 'a' + 26;
        } else if (c >= '0' && c <= '9') {
            digit = c - '0' + 52;
        } else if (c == '+' || c == '-') {
            digit = 62;
        } else if (c == '/' || c == '_') {
            digit = 63;
        } else if (c == '=') {
            break;
        }
        if (digit < 0) {
            continue;
        }
        bits = (bits << 6) | static_cast<uint32_t>(digit);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out.push_back(static_cast<uint8_t>(bits >> count));
        }
    }
    return out;
}

// A FLAC PICTURE block, as FLAC files and Vorbis comments hold it: type,
// MIME type, description, four sizes, then the picture. Returns true for a
// front cover.
bool takeFlacPicture(const uint8_t *data, size_t size, Info &info)
{
    if (size < 32) {
        return false;
    }
    const uint32_t type = be32(data);
    size_t index = 4;
    const uint32_t mimeLength = be32(data + index);
    if (mimeLength > size - index - 4) {
        return false;
    }
    index += 4 + mimeLength;
    const uint32_t descriptionLength = be32(data + index);
    if (descriptionLength > size - index - 4) {
        return false;
    }
    index += 4 + descriptionLength;
    if (size - index < 20) {
        return false;
    }
    index += 16; // width, height, depth and palette size
    const uint32_t length = be32(data + index);
    index += 4;
    if (length > size - index) {
        return false;
    }
    if (info.cover.empty() || type == 3) {
        keepCover(info, std::vector<uint8_t>(data + index,
                                             data + index + length));
    }
    return type == 3;
}

// A Vorbis comment block from offset on: a vendor string, then KEY=value
// entries, all with little-endian lengths.
void readVorbisComment(const std::vector<uint8_t> &block, size_t offset,
                       Info &info, bool withCover)
{
    if (offset + 4 > block.size()) {
        return;
    }
    size_t index = offset;
    const uint32_t vendor = le32(block.data() + index);
    if (vendor > block.size() - index - 4) {
        return;
    }
    index += 4 + vendor;
    if (index + 4 > block.size()) {
        return;
    }
    const uint32_t count = le32(block.data() + index);
    index += 4;
    bool haveFront = false;
    for (uint32_t entry = 0; entry < count && index + 4 <= block.size();
         ++entry) {
        const uint32_t length = le32(block.data() + index);
        index += 4;
        if (length > block.size() - index) {
            break;
        }
        const char *start = reinterpret_cast<const char *>(block.data() + index);
        index += length;
        const char *equals =
            static_cast<const char *>(std::memchr(start, '=', length));
        if (equals == nullptr) {
            continue;
        }
        const std::string key = text::lower(std::string(start, equals));
        const std::string raw(equals + 1, start + length);
        if (key == "metadata_block_picture" || key == "coverart") {
            if (!withCover || haveFront) {
                continue;
            }
            const std::vector<uint8_t> bytes = base64(raw);
            if (key == "coverart") {
                if (info.cover.empty()) {
                    keepCover(info, bytes);
                }
            } else {
                haveFront = takeFlacPicture(bytes.data(), bytes.size(), info);
            }
            continue;
        }
        const std::string value = clean(raw);
        if (value.empty()) {
            continue;
        }
        // The first of repeated fields, such as several ARTIST entries.
        const auto first = [&](std::string &field) {
            if (field.empty()) {
                field = value;
            }
        };
        if (key == "title") {
            first(info.title);
        } else if (key == "artist") {
            first(info.artist);
        } else if (key == "albumartist" || key == "album artist" ||
                   key == "album_artist") {
            first(info.albumArtist);
        } else if (key == "album") {
            first(info.album);
        } else if ((key == "date" || key == "year") && info.year.empty()) {
            info.year = yearOf(value);
        } else if (key == "genre") {
            first(info.genre);
        } else if (key == "tracknumber") {
            takeCount(value, info.track, info.tracks);
        } else if ((key == "tracktotal" || key == "totaltracks") &&
                   info.tracks == 0) {
            info.tracks = leadingNumber(value);
        } else if (key == "discnumber") {
            takeCount(value, info.disc, info.discs);
        } else if ((key == "disctotal" || key == "totaldiscs") &&
                   info.discs == 0) {
            info.discs = leadingNumber(value);
        }
    }
}

// --- Ogg ---------------------------------------------------------------------

struct OggPage {
    uint32_t serial = 0;
    uint64_t granule = 0;
    std::vector<uint8_t> lacing;
    uint64_t body = 0; // where its data starts
    uint64_t next = 0; // where the next page starts
};

bool readOggPage(const cia::Reader &read, uint64_t size, uint64_t offset,
                 OggPage &page)
{
    uint8_t header[27];
    if (offset + 27 > size || !read(offset, header, 27) ||
        std::memcmp(header, "OggS", 4) != 0 || header[4] != 0) {
        return false;
    }
    page.granule = le64(header + 6);
    page.serial = le32(header + 14);
    page.lacing = readUpTo(read, size, offset + 27, header[26]);
    if (page.lacing.size() != header[26]) {
        return false;
    }
    page.body = offset + 27 + header[26];
    uint64_t length = 0;
    for (const uint8_t value : page.lacing) {
        length += value;
    }
    page.next = page.body + length;
    return page.next <= size;
}

bool readOgg(const cia::Reader &read, uint64_t size, Info &info,
             bool withCover)
{
    // The first two packets of the first stream: the identification header
    // and the comments, which may run over many pages when they hold a
    // cover.
    std::vector<uint8_t> packets[2];
    int done = 0;
    uint64_t offset = 0;
    OggPage page;
    uint32_t serial = 0;
    while (done < 2 && readOggPage(read, size, offset, page)) {
        if (offset == 0) {
            serial = page.serial;
        }
        if (page.serial == serial) {
            const std::vector<uint8_t> body =
                readUpTo(read, size, page.body, page.next - page.body);
            size_t at = 0;
            for (const uint8_t value : page.lacing) {
                if (done >= 2 || at + value > body.size()) {
                    break;
                }
                std::vector<uint8_t> &packet = packets[done];
                if (packet.size() + value > MaxTag) {
                    return false;
                }
                packet.insert(packet.end(), body.begin() + static_cast<long>(at),
                              body.begin() + static_cast<long>(at + value));
                at += value;
                if (value < 255) {
                    ++done;
                }
            }
        }
        offset = page.next;
    }
    const std::vector<uint8_t> &head = packets[0];
    const std::vector<uint8_t> &comment = packets[1];
    uint32_t preSkip = 0;
    if (head.size() >= 28 && std::memcmp(head.data(), "\x01vorbis", 7) == 0) {
        info.codec = "Vorbis";
        info.channels = head[11];
        info.sampleRate = le32(head.data() + 12);
        if (comment.size() > 7 &&
            std::memcmp(comment.data(), "\x03vorbis", 7) == 0) {
            readVorbisComment(comment, 7, info, withCover);
        }
    } else if (head.size() >= 19 && std::memcmp(head.data(), "OpusHead", 8) == 0) {
        info.codec = "Opus";
        info.channels = head[9];
        preSkip = le16(head.data() + 10);
        info.sampleRate = 48000; // Opus always decodes at 48 kHz
        if (comment.size() > 8 && std::memcmp(comment.data(), "OpusTags", 8) == 0) {
            readVorbisComment(comment, 8, info, withCover);
        }
    } else {
        return false;
    }

    // The length is the last page's position, in samples.
    const uint64_t window = std::min(size, SearchWindow);
    const std::vector<uint8_t> tail = readUpTo(read, size, size - window, window);
    for (size_t index = tail.size() >= 27 ? tail.size() - 27 + 1 : 0;
         index-- > 0;) {
        const uint8_t *at = tail.data() + index;
        if (std::memcmp(at, "OggS", 4) != 0 || at[4] != 0 ||
            le32(at + 14) != serial) {
            continue;
        }
        const uint64_t granule = le64(at + 6);
        if (granule == ~0ULL || info.sampleRate == 0) {
            continue;
        }
        const uint64_t samples = granule > preSkip ? granule - preSkip : 0;
        info.milliseconds = samples * 1000 / info.sampleRate;
        break;
    }
    if (info.milliseconds > 0 && offset < size) {
        info.kilobits = static_cast<uint32_t>(
            ((size - offset) * 8 + info.milliseconds / 2) / info.milliseconds);
    }
    return true;
}

// --- FLAC --------------------------------------------------------------------

bool readFlac(const cia::Reader &read, uint64_t size, Info &info,
              bool withCover)
{
    // Some taggers put an ID3v2 tag in front; FLAC's own comments win.
    Info id3;
    uint64_t offset = readId3v2(read, size, 0, id3, false);
    uint8_t magic[4];
    if (offset + 4 > size || !read(offset, magic, 4) ||
        std::memcmp(magic, "fLaC", 4) != 0) {
        return false;
    }
    info.codec = "FLAC";
    offset += 4;
    bool last = false;
    bool haveFront = false;
    while (!last && offset + 4 <= size) {
        uint8_t header[4];
        if (!read(offset, header, 4)) {
            break;
        }
        last = (header[0] & 0x80) != 0;
        const int type = header[0] & 0x7f;
        const uint32_t length = be24(header + 1);
        const uint64_t body = offset + 4;
        offset = body + length;
        if (type == 0 && length >= 34) {
            uint8_t stream[34];
            if (!read(body, stream, 34)) {
                break;
            }
            // 20 bits of sample rate, 3 of channels, 5 of sample size and
            // 36 of total samples.
            info.sampleRate = (static_cast<uint32_t>(stream[10]) << 12) |
                              (static_cast<uint32_t>(stream[11]) << 4) |
                              (stream[12] >> 4);
            info.channels = ((stream[12] >> 1) & 7) + 1;
            info.bits = (((stream[12] & 1) << 4) | (stream[13] >> 4)) + 1;
            const uint64_t samples =
                (static_cast<uint64_t>(stream[13] & 0x0f) << 32) |
                be32(stream + 14);
            if (info.sampleRate > 0) {
                info.milliseconds = samples * 1000 / info.sampleRate;
            }
        } else if (type == 4 && length <= MaxTag) {
            readVorbisComment(readUpTo(read, size, body, length), 0, info,
                              withCover);
        } else if (type == 6 && withCover && !haveFront &&
                   length <= MaxCover + 4096) {
            const std::vector<uint8_t> block = readUpTo(read, size, body, length);
            haveFront = takeFlacPicture(block.data(), block.size(), info);
        }
    }
    const auto fill = [](std::string &field, const std::string &value) {
        if (field.empty()) {
            field = value;
        }
    };
    fill(info.title, id3.title);
    fill(info.artist, id3.artist);
    fill(info.album, id3.album);
    return true;
}

// --- WAV ---------------------------------------------------------------------

bool readWav(const cia::Reader &read, uint64_t size, Info &info,
             bool withCover)
{
    uint8_t riff[12];
    if (size < 12 || !read(0, riff, 12) || std::memcmp(riff, "RIFF", 4) != 0 ||
        std::memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }
    info.codec = "WAV";
    uint32_t byteRate = 0;
    uint64_t dataSize = 0;
    uint64_t offset = 12;
    while (offset + 8 <= size) {
        uint8_t chunk[8];
        if (!read(offset, chunk, 8)) {
            break;
        }
        const uint64_t body = offset + 8;
        uint64_t length = le32(chunk + 4);
        if (std::memcmp(chunk, "data", 4) == 0) {
            // Recorders that never finished leave the size at 0 or past
            // the end; the data runs to the end then.
            if (length == 0 || length > size - body) {
                length = size - body;
            }
            dataSize = length;
        } else if (std::memcmp(chunk, "fmt ", 4) == 0 && length >= 16) {
            const std::vector<uint8_t> format =
                readUpTo(read, size, body, std::min<uint64_t>(length, 40));
            if (format.size() >= 16) {
                uint16_t tag = le16(format.data());
                if (tag == 0xfffe && format.size() >= 26) {
                    tag = le16(format.data() + 24); // the subformat's own tag
                }
                info.channels = le16(format.data() + 2);
                info.sampleRate = le32(format.data() + 4);
                byteRate = le32(format.data() + 8);
                if (tag == 1 || tag == 3) {
                    info.bits = le16(format.data() + 14);
                }
                switch (tag) {
                case 2:
                    info.encoding = "MS-ADPCM";
                    break;
                case 3:
                    info.encoding = "Float";
                    break;
                case 6:
                    info.encoding = "A-law";
                    break;
                case 7:
                    info.encoding = "Mu-law";
                    break;
                case 0x11:
                    info.encoding = "IMA-ADPCM";
                    break;
                case 0x55:
                    info.encoding = "MP3";
                    break;
                default:
                    break;
                }
            }
        } else if (std::memcmp(chunk, "LIST", 4) == 0 && length >= 4) {
            const std::vector<uint8_t> list =
                readUpTo(read, size, body, std::min<uint64_t>(length, 65536));
            if (list.size() >= 4 && std::memcmp(list.data(), "INFO", 4) == 0) {
                for (size_t at = 4; at + 8 <= list.size();) {
                    const std::string id(reinterpret_cast<const char *>(list.data() + at), 4);
                    const uint32_t count = le32(list.data() + at + 4);
                    at += 8;
                    if (count > list.size() - at) {
                        break;
                    }
                    // RIFF text has no set encoding; UTF-8 is the usual one.
                    const std::string value =
                        id3Text(list.data() + at, count, 3);
                    if (id == "INAM") {
                        info.title = value;
                    } else if (id == "IART") {
                        info.artist = value;
                    } else if (id == "IPRD") {
                        info.album = value;
                    } else if (id == "ICRD") {
                        info.year = yearOf(value);
                    } else if (id == "IGNR") {
                        info.genre = value;
                    } else if (id == "ITRK" || id == "IPRT") {
                        takeCount(value, info.track, info.tracks);
                    }
                    at += count + (count & 1);
                }
            }
        } else if (std::memcmp(chunk, "id3 ", 4) == 0 ||
                   std::memcmp(chunk, "ID3 ", 4) == 0) {
            readId3v2(read, size, body, info, withCover);
        }
        offset = body + length + (length & 1);
    }
    if (byteRate > 0) {
        info.milliseconds = dataSize * 1000 / byteRate;
    }
    return true;
}

// --- MP4 ---------------------------------------------------------------------

struct Box {
    char type[4] = {};
    uint64_t body = 0; // after the header
    uint64_t end = 0;
    bool is(const char *name) const { return std::memcmp(type, name, 4) == 0; }
};

// The box at offset, inside a parent that ends at end.
bool readBox(const cia::Reader &read, uint64_t offset, uint64_t end, Box &box)
{
    uint8_t header[16];
    if (offset + 8 > end || !read(offset, header, 8)) {
        return false;
    }
    std::memcpy(box.type, header + 4, 4);
    uint64_t length = be32(header);
    box.body = offset + 8;
    if (length == 1) {
        if (offset + 16 > end || !read(offset + 8, header + 8, 8)) {
            return false;
        }
        length = be64(header + 8);
        box.body = offset + 16;
    } else if (length == 0) {
        length = end - offset; // runs to the end
    }
    box.end = offset + length;
    return length >= box.body - offset && box.end <= end;
}

// Calls take for each box in [begin, end), at most a few thousand.
template <typename Take>
void eachBox(const cia::Reader &read, uint64_t begin, uint64_t end, Take take)
{
    Box box;
    for (int count = 0; count < 4096 && readBox(read, begin, end, box); ++count) {
        take(box);
        begin = box.end;
    }
}

// A full box's version, then mvhd's or mdhd's time scale and duration.
uint64_t mediaMilliseconds(const std::vector<uint8_t> &header)
{
    if (header.size() < 24) {
        return 0;
    }
    const bool wide = header[0] == 1;
    if (wide && header.size() < 32) {
        return 0;
    }
    const uint32_t scale = be32(header.data() + (wide ? 20 : 12));
    const uint64_t duration =
        wide ? be64(header.data() + 24) : be32(header.data() + 16);
    return scale > 0 ? duration * 1000 / scale : 0;
}

void readSampleEntry(const cia::Reader &read, uint64_t size, const Box &stsd,
                     Info &info)
{
    // A full box header and an entry count, then the first entry: its size
    // and codec, six reserved bytes, a reference index, eight more reserved,
    // the channel count, the sample size, four more and the 16.16 rate.
    const uint64_t first = stsd.body + 8;
    const std::vector<uint8_t> entry = readUpTo(
        read, size, first,
        stsd.end > first ? std::min<uint64_t>(stsd.end - first, 256) : 0);
    if (entry.size() < 36) {
        return;
    }
    const std::string codec(reinterpret_cast<const char *>(entry.data() + 4), 4);
    info.channels = be16(entry.data() + 24);
    const int sampleSize = be16(entry.data() + 26);
    info.sampleRate = be32(entry.data() + 32) >> 16;
    // QuickTime's version 1 entries carry 16 more bytes before any child.
    const size_t child = 36 + (be16(entry.data() + 16) == 1 ? 16 : 0);
    if (codec == "mp4a") {
        info.codec = "AAC";
    } else if (codec == "alac") {
        info.codec = "ALAC";
        info.bits = sampleSize;
        // ALAC's own header after the entry knows rates past 65535 Hz.
        if (entry.size() >= child + 12 + 24 &&
            std::memcmp(entry.data() + child + 4, "alac", 4) == 0) {
            const uint8_t *cookie = entry.data() + child + 12;
            info.bits = cookie[5];
            info.channels = cookie[9];
            info.sampleRate = be32(cookie + 20);
        }
    } else if (codec == "ac-3") {
        info.codec = "AC-3";
    } else if (codec == "Opus") {
        info.codec = "Opus";
    } else if (codec == "fLaC") {
        info.codec = "FLAC";
        info.bits = sampleSize;
    } else if (codec == ".mp3") {
        info.codec = "MP3";
    } else {
        info.codec = clean(codec);
    }
}

void readIlst(const cia::Reader &read, uint64_t size, const Box &ilst,
              Info &info, bool withCover)
{
    eachBox(read, ilst.body, ilst.end, [&](const Box &item) {
        // Each item holds a data box: its type, a locale, then the value.
        Box data;
        if (!readBox(read, item.body, item.end, data) || !data.is("data") ||
            data.end - data.body < 8) {
            return;
        }
        const uint64_t valueAt = data.body + 8;
        const uint64_t valueSize = data.end - valueAt;
        if (std::memcmp(item.type, "covr", 4) == 0) {
            if (withCover && info.cover.empty() && valueSize <= MaxCover) {
                keepCover(info, readUpTo(read, size, valueAt, valueSize));
            }
            return;
        }
        if (valueSize > 4096) {
            return;
        }
        const std::vector<uint8_t> value =
            readUpTo(read, size, valueAt, valueSize);
        const std::string textValue =
            clean(std::string(value.begin(), value.end()));
        const char *const type = item.type;
        if (std::memcmp(type, "\xa9nam", 4) == 0) {
            info.title = textValue;
        } else if (std::memcmp(type, "\xa9" "ART", 4) == 0) {
            info.artist = textValue;
        } else if (std::memcmp(type, "aART", 4) == 0) {
            info.albumArtist = textValue;
        } else if (std::memcmp(type, "\xa9" "alb", 4) == 0) {
            info.album = textValue;
        } else if (std::memcmp(type, "\xa9" "day", 4) == 0) {
            info.year = yearOf(textValue);
        } else if (std::memcmp(type, "\xa9gen", 4) == 0) {
            info.genre = textValue;
        } else if (std::memcmp(type, "gnre", 4) == 0 && value.size() >= 2 &&
                   info.genre.empty()) {
            info.genre = genreNumber(be16(value.data()) - 1);
        } else if ((std::memcmp(type, "trkn", 4) == 0 ||
                    std::memcmp(type, "disk", 4) == 0) &&
                   value.size() >= 6) {
            const bool track = type[0] == 't';
            (track ? info.track : info.disc) = be16(value.data() + 2);
            (track ? info.tracks : info.discs) = be16(value.data() + 4);
        }
    });
}

void readMeta(const cia::Reader &read, uint64_t size, const Box &meta,
              Info &info, bool withCover)
{
    // iTunes writes meta as a full box, with four bytes of version and
    // flags before its children; QuickTime does not.
    uint8_t peek[8];
    uint64_t begin = meta.body;
    if (meta.body + 8 <= meta.end && read(meta.body, peek, 8) &&
        std::memcmp(peek + 4, "hdlr", 4) != 0) {
        begin += 4;
    }
    eachBox(read, begin, meta.end, [&](const Box &box) {
        if (box.is("ilst")) {
            readIlst(read, size, box, info, withCover);
        }
    });
}

bool readMp4(const cia::Reader &read, uint64_t size, Info &info,
             bool withCover)
{
    Box first;
    if (!readBox(read, 0, size, first) ||
        !(first.is("ftyp") || first.is("moov") || first.is("mdat") ||
          first.is("free") || first.is("skip") || first.is("wide"))) {
        return false;
    }
    uint64_t mediaBytes = 0;
    uint64_t movieMilliseconds = 0;
    uint64_t trackMilliseconds = 0;
    bool haveTrack = false;
    eachBox(read, 0, size, [&](const Box &top) {
        if (top.is("mdat")) {
            mediaBytes += top.end - top.body;
        }
        if (!top.is("moov")) {
            return;
        }
        eachBox(read, top.body, top.end, [&](const Box &box) {
            if (box.is("mvhd")) {
                movieMilliseconds =
                    mediaMilliseconds(readUpTo(read, size, box.body, 32));
            } else if (box.is("udta")) {
                eachBox(read, box.body, box.end, [&](const Box &child) {
                    if (child.is("meta")) {
                        readMeta(read, size, child, info, withCover);
                    }
                });
            } else if (box.is("meta")) {
                readMeta(read, size, box, info, withCover);
            } else if (box.is("trak") && !haveTrack) {
                // The first sound track: mdia holds hdlr, mdhd and the
                // sample description under minf/stbl/stsd.
                eachBox(read, box.body, box.end, [&](const Box &mdia) {
                    if (!mdia.is("mdia")) {
                        return;
                    }
                    bool sound = false;
                    uint64_t milliseconds = 0;
                    Box stsd;
                    bool haveStsd = false;
                    eachBox(read, mdia.body, mdia.end, [&](const Box &child) {
                        if (child.is("hdlr")) {
                            const std::vector<uint8_t> handler =
                                readUpTo(read, size, child.body, 12);
                            sound = handler.size() == 12 &&
                                    std::memcmp(handler.data() + 8, "soun", 4) == 0;
                        } else if (child.is("mdhd")) {
                            milliseconds = mediaMilliseconds(
                                readUpTo(read, size, child.body, 32));
                        } else if (child.is("minf")) {
                            eachBox(read, child.body, child.end, [&](const Box &stbl) {
                                if (!stbl.is("stbl")) {
                                    return;
                                }
                                eachBox(read, stbl.body, stbl.end, [&](const Box &leaf) {
                                    if (leaf.is("stsd")) {
                                        stsd = leaf;
                                        haveStsd = true;
                                    }
                                });
                            });
                        }
                    });
                    if (sound) {
                        haveTrack = true;
                        trackMilliseconds = milliseconds;
                        if (haveStsd) {
                            readSampleEntry(read, size, stsd, info);
                        }
                    }
                });
            }
        });
    });
    if (!haveTrack) {
        return false;
    }
    info.milliseconds = trackMilliseconds > 0 ? trackMilliseconds
                                              : movieMilliseconds;
    if (info.bits == 0 && info.milliseconds > 0 && mediaBytes > 0) {
        info.kilobits = static_cast<uint32_t>(
            (mediaBytes * 8 + info.milliseconds / 2) / info.milliseconds);
    }
    return true;
}

// --- Raw AAC -----------------------------------------------------------------

bool readAac(const cia::Reader &read, uint64_t size, Info &info,
             bool withCover)
{
    static const uint32_t Rates[] = {96000, 88200, 64000, 48000, 44100,
                                     32000, 24000, 22050, 16000, 12000,
                                     11025, 8000,  7350};
    const uint64_t start = readId3v2(read, size, 0, info, withCover);
    uint8_t header[7];
    if (start + 7 > size || !read(start, header, 7) || header[0] != 0xff ||
        (header[1] & 0xf6) != 0xf0) {
        return false;
    }
    info.codec = "AAC";
    const int rateIndex = (header[2] >> 2) & 0x0f;
    if (rateIndex < 13) {
        info.sampleRate = Rates[rateIndex];
    }
    info.channels = ((header[2] & 1) << 2) | (header[3] >> 6);
    // Its length would mean walking every frame, so it is left out.
    return true;
}

// --- 3DS streams and sounds ----------------------------------------------------

const char *dspEncoding(int encoding, Info &info)
{
    switch (encoding) {
    case 0:
        info.bits = 8;
        return "";
    case 1:
        info.bits = 16;
        return "";
    case 2:
        return "DSP-ADPCM";
    case 3:
        return "IMA-ADPCM";
    default:
        return "";
    }
}

void takeLoop(uint32_t sampleRate, bool loops, uint32_t loopStart,
              uint32_t frames, Info &info)
{
    info.sampleRate = sampleRate;
    if (sampleRate == 0) {
        return;
    }
    info.milliseconds = static_cast<uint64_t>(frames) * 1000 / sampleRate;
    info.loops = loops && frames > loopStart;
    if (info.loops) {
        info.loopStart = static_cast<uint64_t>(loopStart) * 1000 / sampleRate;
        info.loopEnd = info.milliseconds;
    }
}

// BCSTM and BCWAV share a header: magic, byte order mark, header size,
// version, file size, block count, then the blocks' references.
bool readCtrHeader(const cia::Reader &read, uint64_t size, const char *magic,
                   uint16_t infoType, uint64_t &infoAt)
{
    uint8_t header[0x40];
    if (size < sizeof(header) || !read(0, header, sizeof(header)) ||
        std::memcmp(header, magic, 4) != 0 || le16(header + 4) != 0xfeff) {
        return false;
    }
    // Room for three references, which is all either format has.
    const int blocks = std::min<int>(le16(header + 0x10), 3);
    for (int block = 0; block < blocks; ++block) {
        const uint8_t *reference = header + 0x14 + block * 12;
        if (le16(reference) == infoType) {
            infoAt = le32(reference + 4);
            return true;
        }
    }
    return false;
}

bool readBcstm(const cia::Reader &read, uint64_t size, Info &info)
{
    uint64_t infoAt = 0;
    uint8_t block[0x20];
    if (!readCtrHeader(read, size, "CSTM", 0x4000, infoAt) ||
        infoAt + sizeof(block) > size || !read(infoAt, block, sizeof(block)) ||
        std::memcmp(block, "INFO", 4) != 0 || le16(block + 8) != 0x4100) {
        return false;
    }
    // The stream info, at an offset from the end of the block's header.
    uint8_t stream[0x10];
    const uint64_t streamAt = infoAt + 8 + le32(block + 12);
    if (streamAt + sizeof(stream) > size || !read(streamAt, stream, sizeof(stream))) {
        return false;
    }
    info.codec = "BCSTM";
    info.encoding = dspEncoding(stream[0], info);
    info.channels = stream[2];
    takeLoop(le32(stream + 4), stream[1] != 0, le32(stream + 8),
             le32(stream + 12), info);
    return true;
}

bool readBcwav(const cia::Reader &read, uint64_t size, Info &info)
{
    uint64_t infoAt = 0;
    uint8_t block[0x20];
    if (!readCtrHeader(read, size, "CWAV", 0x7000, infoAt) ||
        infoAt + sizeof(block) > size || !read(infoAt, block, sizeof(block)) ||
        std::memcmp(block, "INFO", 4) != 0) {
        return false;
    }
    info.codec = "BCWAV";
    info.encoding = dspEncoding(block[8], info);
    // The channel count is the length of the channel table after the loop.
    info.channels = static_cast<int>(std::min<uint32_t>(le32(block + 0x1c), 64));
    takeLoop(le32(block + 0x0c), block[9] != 0, le32(block + 0x10),
             le32(block + 0x14), info);
    return true;
}

// --- Tracker modules -------------------------------------------------------------

bool readModule(const cia::Reader &read, uint64_t size, Format format,
                Info &info)
{
    const std::vector<uint8_t> head = readUpTo(read, size, 0, 1084);
    const auto has = [&](size_t at, const char *magic) {
        const size_t length = std::strlen(magic);
        return head.size() >= at + length &&
               std::memcmp(head.data() + at, magic, length) == 0;
    };
    switch (format) {
    case Format::Mod: {
        if (head.size() < 1084) {
            return false;
        }
        info.codec = "MOD";
        info.title = latin1(head.data(), 20);
        const char *tag = reinterpret_cast<const char *>(head.data() + 1080);
        if (has(1080, "M.K.") || has(1080, "M!K!") || has(1080, "FLT4")) {
            info.channels = 4;
        } else if (has(1080, "FLT8")) {
            info.channels = 8;
        } else if (std::memcmp(tag + 1, "CHN", 3) == 0 && tag[0] >= '1' &&
                   tag[0] <= '9') {
            info.channels = tag[0] - '0';
        } else if (tag[2] == 'C' && tag[3] == 'H' && tag[0] >= '1' &&
                   tag[0] <= '9' && tag[1] >= '0' && tag[1] <= '9') {
            info.channels = (tag[0] - '0') * 10 + (tag[1] - '0');
        }
        return true;
    }
    case Format::Xm:
        if (!has(0, "Extended Module: ") || head.size() < 70) {
            return false;
        }
        info.codec = "XM";
        info.title = latin1(head.data() + 17, 20);
        info.channels = le16(head.data() + 68);
        return true;
    case Format::S3m:
        if (!has(44, "SCRM") || head.size() < 96) {
            return false;
        }
        info.codec = "S3M";
        info.title = latin1(head.data(), 28);
        // Channels in use have a setting below 16.
        info.channels = static_cast<int>(std::count_if(
            head.begin() + 64, head.begin() + 96, [](uint8_t value) { return value < 16; }));
        return true;
    case Format::It:
        if (!has(0, "IMPM") || head.size() < 30) {
            return false;
        }
        info.codec = "IT";
        info.title = latin1(head.data() + 4, 26);
        return true;
    default:
        return false;
    }
}

// --- Text ----------------------------------------------------------------------

std::string clockText(uint64_t milliseconds, bool fractions)
{
    char buffer[32];
    if (fractions) {
        const uint64_t tenths = (milliseconds + 50) / 100;
        std::snprintf(buffer, sizeof(buffer), "%llu.%llus",
                      static_cast<unsigned long long>(tenths / 10),
                      static_cast<unsigned long long>(tenths % 10));
        return buffer;
    }
    const uint64_t seconds = (milliseconds + 500) / 1000;
    if (seconds >= 3600) {
        std::snprintf(buffer, sizeof(buffer), "%llu:%02u:%02u",
                      static_cast<unsigned long long>(seconds / 3600),
                      static_cast<unsigned>(seconds / 60 % 60),
                      static_cast<unsigned>(seconds % 60));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%u:%02u",
                      static_cast<unsigned>(seconds / 60),
                      static_cast<unsigned>(seconds % 60));
    }
    return buffer;
}

// Short sounds are told in tenths of a second, songs as a clock.
constexpr uint64_t ShortSound = 10000;

std::string rateText(uint32_t rate)
{
    char buffer[32];
    if (rate % 1000 == 0) {
        std::snprintf(buffer, sizeof(buffer), "%u kHz",
                      static_cast<unsigned>(rate / 1000));
    } else if (rate % 100 == 0) {
        std::snprintf(buffer, sizeof(buffer), "%u.%u kHz",
                      static_cast<unsigned>(rate / 1000),
                      static_cast<unsigned>(rate % 1000 / 100));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%u Hz",
                      static_cast<unsigned>(rate));
    }
    return buffer;
}

} // namespace

Format formatOf(const std::string &name)
{
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return Format::Unknown;
    }
    const std::string extension = text::lower(name.substr(dot + 1));
    static const struct {
        const char *extension;
        Format format;
    } Formats[] = {{"mp3", Format::Mp3},     {"ogg", Format::Ogg},
                   {"oga", Format::Ogg},     {"opus", Format::Ogg},
                   {"flac", Format::Flac},   {"wav", Format::Wav},
                   {"m4a", Format::Mp4},     {"aac", Format::Aac},
                   {"bcstm", Format::Bcstm}, {"bcwav", Format::Bcwav},
                   {"mod", Format::Mod},     {"xm", Format::Xm},
                   {"s3m", Format::S3m},     {"it", Format::It}};
    for (const auto &entry : Formats) {
        if (extension == entry.extension) {
            return entry.format;
        }
    }
    return Format::Unknown;
}

bool read(const cia::Reader &read, uint64_t size, Format format, Info &info,
          bool withCover)
{
    info = Info();
    bool ok = false;
    switch (format) {
    case Format::Mp3:
        ok = readMp3(read, size, info, withCover);
        break;
    case Format::Ogg:
        ok = readOgg(read, size, info, withCover);
        break;
    case Format::Flac:
        ok = readFlac(read, size, info, withCover);
        break;
    case Format::Wav:
        ok = readWav(read, size, info, withCover);
        break;
    case Format::Mp4:
        ok = readMp4(read, size, info, withCover);
        break;
    case Format::Aac:
        ok = readAac(read, size, info, withCover);
        break;
    case Format::Bcstm:
        ok = readBcstm(read, size, info);
        break;
    case Format::Bcwav:
        ok = readBcwav(read, size, info);
        break;
    case Format::Mod:
    case Format::Xm:
    case Format::S3m:
    case Format::It:
        ok = readModule(read, size, format, info);
        break;
    case Format::Unknown:
        break;
    }
    if (!ok) {
        info = Info();
    }
    return ok;
}

bool read(const std::string &path, Info &info, bool withCover)
{
    const Format format = formatOf(path);
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        info = Info();
        return false;
    }
    uint64_t size = 0;
    if (fseeko(file, 0, SEEK_END) == 0) {
        const off_t end = ftello(file);
        size = end > 0 ? static_cast<uint64_t>(end) : 0;
    }
    // The parsers read many small pieces in order, and every call to the
    // card costs about the same whatever its size, so small reads are
    // served from one window of the file.
    constexpr size_t Window = 32 * 1024;
    std::vector<uint8_t> window;
    uint64_t windowAt = 0;
    const cia::Reader reader = [&](uint64_t offset, void *buffer, size_t count) {
        if (count > Window) {
            return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0 &&
                   std::fread(buffer, 1, count, file) == count;
        }
        if (offset < windowAt || offset + count > windowAt + window.size()) {
            window.resize(static_cast<size_t>(
                std::min<uint64_t>(Window, size > offset ? size - offset : 0)));
            windowAt = offset;
            if (window.size() < count ||
                fseeko(file, static_cast<off_t>(offset), SEEK_SET) != 0 ||
                std::fread(window.data(), 1, window.size(), file) != window.size()) {
                window.clear();
                return false;
            }
        }
        std::memcpy(buffer, window.data() + (offset - windowAt), count);
        return true;
    };
    const bool ok = read(reader, size, format, info, withCover);
    std::fclose(file);
    return ok;
}

std::string length(uint64_t milliseconds)
{
    if (milliseconds == 0) {
        return "";
    }
    return clockText(milliseconds, milliseconds < ShortSound);
}

std::string formatText(const Info &info)
{
    std::string out = info.codec;
    const auto add = [&](const std::string &part) {
        if (!part.empty()) {
            out += (out.empty() ? "" : ", ") + part;
        }
    };
    add(info.encoding);
    if (info.bits > 0) {
        add(std::to_string(info.bits) + "-bit");
    } else if (info.kilobits > 0) {
        add(std::to_string(info.kilobits) + " kbps");
    }
    if (info.sampleRate > 0) {
        add(rateText(info.sampleRate));
    }
    if (info.channels == 1) {
        add("mono");
    } else if (info.channels > 2) {
        add(std::to_string(info.channels) + " channels");
    }
    return out;
}

std::string trackText(const Info &info)
{
    std::string out;
    if (info.track > 0) {
        out = std::to_string(info.track);
        if (info.tracks >= info.track) {
            out += " of " + std::to_string(info.tracks);
        }
    }
    if (info.disc > 0 && (info.discs > 1 || info.disc > 1)) {
        out += out.empty() ? "Disc " : ", disc ";
        out += std::to_string(info.disc);
        if (info.discs >= info.disc) {
            out += " of " + std::to_string(info.discs);
        }
    }
    return out;
}

std::string albumText(const Info &info)
{
    if (info.album.empty()) {
        return info.year;
    }
    return info.year.empty() ? info.album
                             : info.album + " (" + info.year + ")";
}

std::string loopText(const Info &info)
{
    if (!info.loops) {
        return "";
    }
    const bool fractions = info.loopEnd < ShortSound;
    return clockText(info.loopStart, fractions) + " to " +
           clockText(info.loopEnd, fractions);
}

int coverRank(const std::string &name)
{
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return 0;
    }
    const std::string extension = text::lower(name.substr(dot + 1));
    if (extension != "jpg" && extension != "jpeg" && extension != "png") {
        return 0;
    }
    const std::string stem = text::lower(name.substr(0, dot));
    static const char *const Names[] = {"albumart", "album", "front", "folder",
                                        "cover"};
    for (size_t index = 0; index < sizeof(Names) / sizeof(Names[0]); ++index) {
        if (stem == Names[index]) {
            return static_cast<int>(index) + 3;
        }
    }
    // Windows Media Player's AlbumArt_{...}_Large.jpg and AlbumArtSmall.jpg.
    if (text::startsWithNoCase(stem, "albumart")) {
        return stem.find("large") != std::string::npos ? 2 : 1;
    }
    return 0;
}

} // namespace audiometa
