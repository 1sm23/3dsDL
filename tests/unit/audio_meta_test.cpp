#include "../check.hpp"

#include "audio_meta.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

void put(Bytes &out, const std::string &value)
{
    out.insert(out.end(), value.begin(), value.end());
}

void put(Bytes &out, const Bytes &value)
{
    out.insert(out.end(), value.begin(), value.end());
}

void putBe(Bytes &out, uint64_t value, int bytes)
{
    for (int index = bytes - 1; index >= 0; --index) {
        out.push_back(static_cast<uint8_t>(value >> (index * 8)));
    }
}

void putLe(Bytes &out, uint64_t value, int bytes)
{
    for (int index = 0; index < bytes; ++index) {
        out.push_back(index < 8 ? static_cast<uint8_t>(value >> (index * 8)) : 0);
    }
}

void putSyncsafe(Bytes &out, uint32_t value)
{
    for (int shift = 21; shift >= 0; shift -= 7) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0x7f));
    }
}

// A picture's bytes; only their identity matters here.
Bytes picture(uint8_t fill, size_t size = 300)
{
    Bytes bytes(size, fill);
    bytes[0] = 0xff;
    bytes[1] = 0xd8;
    return bytes;
}

bool readBytes(const Bytes &bytes, audiometa::Format format,
               audiometa::Info &info, bool withCover = true)
{
    const cia::Reader reader = [&](uint64_t offset, void *buffer,
                                   size_t count) {
        if (offset > bytes.size() || count > bytes.size() - offset) {
            return false;
        }
        std::memcpy(buffer, bytes.data() + offset, count);
        return true;
    };
    return audiometa::read(reader, bytes.size(), format, info, withCover);
}

// Every shorter copy of bytes, and copies with bytes flipped, must read
// without going out of bounds (the tests run under AddressSanitizer).
void survivesDamage(const Bytes &bytes, audiometa::Format format)
{
    audiometa::Info info;
    for (size_t size = 0; size < bytes.size(); size += 1 + size / 64) {
        readBytes(Bytes(bytes.begin(), bytes.begin() + static_cast<long>(size)),
                  format, info);
    }
    uint32_t seed = 12345;
    for (int round = 0; round < 200; ++round) {
        Bytes damaged = bytes;
        for (int flip = 0; flip < 4; ++flip) {
            seed = seed * 1103515245 + 12345;
            damaged[(seed >> 8) % damaged.size()] ^= static_cast<uint8_t>(seed >> 24) | 1;
        }
        readBytes(damaged, format, info);
    }
}

// --- MP3 ---------------------------------------------------------------------

Bytes id3Frame(const std::string &id, const Bytes &body, int major = 3,
               uint16_t flags = 0)
{
    Bytes out;
    put(out, id);
    if (major == 2) {
        putBe(out, body.size(), 3);
    } else {
        if (major == 4) {
            putSyncsafe(out, static_cast<uint32_t>(body.size()));
        } else {
            putBe(out, body.size(), 4);
        }
        putBe(out, flags, 2);
    }
    put(out, body);
    return out;
}

Bytes latin1Text(const std::string &value)
{
    Bytes out = {0};
    put(out, value);
    return out;
}

Bytes id3Tag(const Bytes &frames, int major = 3, uint8_t flags = 0)
{
    Bytes out;
    put(out, "ID3");
    out.push_back(static_cast<uint8_t>(major));
    out.push_back(0);
    out.push_back(flags);
    putSyncsafe(out, static_cast<uint32_t>(frames.size() + 20));
    put(out, frames);
    out.resize(out.size() + 20, 0); // padding
    return out;
}

Bytes apic(uint8_t type, const Bytes &image)
{
    Bytes body = {0};
    put(body, "image/jpeg");
    body.push_back(0);
    body.push_back(type);
    put(body, "cover");
    body.push_back(0);
    put(body, image);
    return body;
}

// MPEG-1 Layer III frames at 128 kbps and 44.1 kHz, 417 bytes each.
Bytes mpegFrames(int count, const Bytes &firstPayload = {})
{
    Bytes out;
    for (int frame = 0; frame < count; ++frame) {
        Bytes one = {0xff, 0xfb, 0x90, 0x00};
        one.resize(417, 0);
        if (frame == 0) {
            std::copy(firstPayload.begin(), firstPayload.end(), one.begin() + 4);
        }
        put(out, one);
    }
    return out;
}

} // namespace

TEST("audio meta: ID3v2.3 tags, front cover and a CBR MP3's length")
{
    Bytes utf16 = {1, 0xff, 0xfe};
    for (const char c : std::string("Daft Punk")) {
        utf16.push_back(static_cast<uint8_t>(c));
        utf16.push_back(0);
    }
    Bytes frames;
    put(frames, id3Frame("TIT2", latin1Text("One More Time")));
    put(frames, id3Frame("TPE1", utf16));
    put(frames, id3Frame("TALB", latin1Text("Discovery")));
    put(frames, id3Frame("TYER", latin1Text("2001")));
    put(frames, id3Frame("TRCK", latin1Text("3/12")));
    put(frames, id3Frame("TPOS", latin1Text("1/2")));
    put(frames, id3Frame("TCON", latin1Text("(52)")));
    put(frames, id3Frame("APIC", apic(0, picture(0x11))));
    put(frames, id3Frame("APIC", apic(3, picture(0x33))));
    Bytes file = id3Tag(frames);
    put(file, mpegFrames(100));

    audiometa::Info info;
    CHECK(readBytes(file, audiometa::Format::Mp3, info));
    CHECK_EQ(info.title, std::string("One More Time"));
    CHECK_EQ(info.artist, std::string("Daft Punk"));
    CHECK_EQ(info.album, std::string("Discovery"));
    CHECK_EQ(info.year, std::string("2001"));
    CHECK_EQ(info.genre, std::string("Electronic"));
    CHECK_EQ(info.track, 3);
    CHECK_EQ(info.tracks, 12);
    CHECK_EQ(info.disc, 1);
    CHECK_EQ(info.discs, 2);
    CHECK(info.cover == picture(0x33));
    CHECK_EQ(info.codec, std::string("MP3"));
    CHECK_EQ(info.kilobits, 128u);
    CHECK_EQ(info.sampleRate, 44100u);
    CHECK_EQ(info.channels, 2);
    CHECK_EQ(info.milliseconds, static_cast<uint64_t>(41700 * 8 / 128));
    CHECK_EQ(audiometa::formatText(info), std::string("MP3, 128 kbps, 44.1 kHz"));
    CHECK_EQ(audiometa::trackText(info), std::string("3 of 12, disc 1 of 2"));
    CHECK_EQ(audiometa::albumText(info), std::string("Discovery (2001)"));

    CHECK(readBytes(file, audiometa::Format::Mp3, info, false));
    CHECK(info.cover.empty());
    CHECK_EQ(info.title, std::string("One More Time"));
    survivesDamage(file, audiometa::Format::Mp3);
}

TEST("audio meta: a Xing header gives a VBR MP3's length and average rate")
{
    Bytes xing(32, 0); // side information
    put(xing, "Xing");
    putBe(xing, 3, 4);
    putBe(xing, 1000, 4);   // frames
    putBe(xing, 417000, 4); // bytes
    const Bytes file = mpegFrames(20, xing);
    audiometa::Info info;
    CHECK(readBytes(file, audiometa::Format::Mp3, info));
    CHECK_EQ(info.milliseconds, static_cast<uint64_t>(1000ULL * 1152 * 1000 / 44100));
    CHECK_EQ(info.kilobits, 128u);
}

TEST("audio meta: ID3v2.4 UTF-8, dates, unsynchronised frames and ID3v2.2")
{
    Bytes utf8 = {3};
    put(utf8, "Bj\xc3\xb6rk");
    // A cover with FF 00 inside, unsynchronised with a length indicator.
    Bytes image = picture(0x44);
    image[10] = 0xff;
    image[11] = 0x00;
    Bytes body = apic(3, image);
    Bytes stuffed;
    for (size_t index = 0; index < body.size(); ++index) {
        stuffed.push_back(body[index]);
        if (body[index] == 0xff) {
            stuffed.push_back(0);
        }
    }
    Bytes withLength;
    putSyncsafe(withLength, static_cast<uint32_t>(body.size()));
    put(withLength, stuffed);
    Bytes frames;
    put(frames, id3Frame("TPE1", utf8, 4));
    put(frames, id3Frame("TDRC", latin1Text("1997-09-22"), 4));
    put(frames, id3Frame("TCON", latin1Text("Electronica"), 4));
    put(frames, id3Frame("APIC", withLength, 4, 0x0003));
    Bytes file = id3Tag(frames, 4);
    put(file, mpegFrames(4));

    audiometa::Info info;
    CHECK(readBytes(file, audiometa::Format::Mp3, info));
    CHECK_EQ(info.artist, std::string("Bj\xc3\xb6rk"));
    CHECK_EQ(info.year, std::string("1997"));
    CHECK_EQ(info.genre, std::string("Electronica"));
    CHECK(info.cover == image);

    Bytes old;
    put(old, id3Frame("TT2", latin1Text("Old Tag"), 2));
    Bytes pic = {0};
    put(pic, "JPG");
    pic.push_back(3);
    pic.push_back(0);
    put(pic, picture(0x22));
    put(old, id3Frame("PIC", pic, 2));
    Bytes oldFile = id3Tag(old, 2);
    put(oldFile, mpegFrames(4));
    CHECK(readBytes(oldFile, audiometa::Format::Mp3, info));
    CHECK_EQ(info.title, std::string("Old Tag"));
    CHECK(info.cover == picture(0x22));
    survivesDamage(file, audiometa::Format::Mp3);
    survivesDamage(oldFile, audiometa::Format::Mp3);
}

TEST("audio meta: ID3v1 fills in when there is no ID3v2 tag")
{
    Bytes file = mpegFrames(10);
    Bytes tag(128, 0);
    std::memcpy(tag.data(), "TAG", 3);
    std::memcpy(tag.data() + 3, "Song", 4);
    std::memcpy(tag.data() + 33, "Band", 4);
    std::memcpy(tag.data() + 63, "Record", 6);
    std::memcpy(tag.data() + 93, "1984", 4);
    tag[126] = 7;
    tag[127] = 17;
    put(file, tag);
    audiometa::Info info;
    CHECK(readBytes(file, audiometa::Format::Mp3, info));
    CHECK_EQ(info.title, std::string("Song"));
    CHECK_EQ(info.artist, std::string("Band"));
    CHECK_EQ(info.album, std::string("Record"));
    CHECK_EQ(info.year, std::string("1984"));
    CHECK_EQ(info.track, 7);
    CHECK_EQ(info.genre, std::string("Rock"));
    // The tag is not audio.
    CHECK_EQ(info.milliseconds, static_cast<uint64_t>(4170 * 8 / 128));
}

TEST("audio meta: a file that is not MP3 is refused")
{
    audiometa::Info info;
    CHECK(!readBytes(Bytes(4000, 0x20), audiometa::Format::Mp3, info));
    CHECK(info.codec.empty());
}

// --- Ogg, FLAC ---------------------------------------------------------------

namespace {

std::string base64Text(const Bytes &bytes)
{
    static const char Digits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t index = 0; index < bytes.size(); index += 3) {
        uint32_t chunk = static_cast<uint32_t>(bytes[index]) << 16;
        if (index + 1 < bytes.size()) {
            chunk |= static_cast<uint32_t>(bytes[index + 1]) << 8;
        }
        if (index + 2 < bytes.size()) {
            chunk |= bytes[index + 2];
        }
        out += Digits[(chunk >> 18) & 63];
        out += Digits[(chunk >> 12) & 63];
        out += index + 1 < bytes.size() ? Digits[(chunk >> 6) & 63] : '=';
        out += index + 2 < bytes.size() ? Digits[chunk & 63] : '=';
    }
    return out;
}

Bytes flacPicture(uint32_t type, const Bytes &image)
{
    Bytes out;
    putBe(out, type, 4);
    putBe(out, 10, 4);
    put(out, "image/jpeg");
    putBe(out, 0, 4);
    putBe(out, 500, 4);
    putBe(out, 500, 4);
    putBe(out, 24, 4);
    putBe(out, 0, 4);
    putBe(out, image.size(), 4);
    put(out, image);
    return out;
}

Bytes vorbisComment(const std::vector<std::string> &entries)
{
    Bytes out;
    putLe(out, 4, 4);
    put(out, "test");
    putLe(out, entries.size(), 4);
    for (const std::string &entry : entries) {
        putLe(out, entry.size(), 4);
        put(out, entry);
    }
    return out;
}

// Ogg pages for packets, each packet starting a new page and running over
// as many as it needs. The last page carries granule.
Bytes oggStream(const std::vector<Bytes> &packets, uint64_t granule)
{
    Bytes out;
    uint32_t sequence = 0;
    for (size_t packet = 0; packet < packets.size(); ++packet) {
        std::vector<uint8_t> lacing;
        size_t left = packets[packet].size();
        while (left >= 255) {
            lacing.push_back(255);
            left -= 255;
        }
        lacing.push_back(static_cast<uint8_t>(left));
        size_t at = 0;
        for (size_t first = 0; first < lacing.size(); first += 255) {
            const size_t count = std::min<size_t>(255, lacing.size() - first);
            const bool last = packet + 1 == packets.size() &&
                              first + count == lacing.size();
            put(out, "OggS");
            out.push_back(0);
            out.push_back(first > 0 ? 1 : 0);
            putLe(out, last ? granule : 0, 8);
            putLe(out, 0x1234, 4);
            putLe(out, sequence++, 4);
            putLe(out, 0, 4);
            out.push_back(static_cast<uint8_t>(count));
            size_t body = 0;
            for (size_t index = first; index < first + count; ++index) {
                out.push_back(lacing[index]);
                body += lacing[index];
            }
            out.insert(out.end(), packets[packet].begin() + static_cast<long>(at),
                       packets[packet].begin() + static_cast<long>(at + body));
            at += body;
        }
    }
    return out;
}

} // namespace

TEST("audio meta: Ogg Vorbis comments across pages, cover and length")
{
    Bytes head;
    put(head, "\x01vorbis");
    putLe(head, 0, 4);
    head.push_back(2);
    putLe(head, 44100, 4);
    putLe(head, 0, 12);
    head.push_back(0xb8);
    head.push_back(1);
    Bytes comment;
    put(comment, "\x03vorbis");
    put(comment, vorbisComment(
                     {"METADATA_BLOCK_PICTURE=" +
                          base64Text(flacPicture(3, picture(0x55, 90000))),
                      "TITLE=Song", "ARTIST=First", "ARTIST=Second",
                      "ALBUM=Record", "DATE=2004-01-01", "TRACKNUMBER=2",
                      "TRACKTOTAL=9", "GENRE=Ambient"}));
    comment.push_back(1);
    const Bytes file = oggStream({head, comment, Bytes(5000, 0)}, 441000);

    audiometa::Info info;
    CHECK(readBytes(file, audiometa::Format::Ogg, info));
    CHECK_EQ(info.codec, std::string("Vorbis"));
    CHECK_EQ(info.title, std::string("Song"));
    CHECK_EQ(info.artist, std::string("First"));
    CHECK_EQ(info.album, std::string("Record"));
    CHECK_EQ(info.year, std::string("2004"));
    CHECK_EQ(info.track, 2);
    CHECK_EQ(info.tracks, 9);
    CHECK_EQ(info.genre, std::string("Ambient"));
    CHECK(info.cover == picture(0x55, 90000));
    CHECK_EQ(info.milliseconds, static_cast<uint64_t>(10000));
    CHECK_EQ(info.sampleRate, 44100u);
    CHECK(info.kilobits > 0);
    survivesDamage(oggStream({head, comment}, 441000), audiometa::Format::Ogg);
}

TEST("audio meta: Opus length leaves out the pre-skip")
{
    Bytes head;
    put(head, "OpusHead");
    head.push_back(1);
    head.push_back(1);
    putLe(head, 312, 2);
    putLe(head, 44100, 4);
    putLe(head, 0, 3);
    Bytes tags;
    put(tags, "OpusTags");
    put(tags, vorbisComment({"title=Quiet"}));
    const Bytes file = oggStream({head, tags, Bytes(2000, 0)}, 48000 * 5 + 312);
    audiometa::Info info;
    CHECK(readBytes(file, audiometa::Format::Ogg, info));
    CHECK_EQ(info.codec, std::string("Opus"));
    CHECK_EQ(info.title, std::string("Quiet"));
    CHECK_EQ(info.milliseconds, static_cast<uint64_t>(5000));
    CHECK_EQ(info.channels, 1);
    CHECK_EQ(audiometa::formatText(info).find("Opus, "), static_cast<size_t>(0));
    CHECK(audiometa::formatText(info).find("48 kHz, mono") != std::string::npos);
}

TEST("audio meta: FLAC stream info, comments and pictures")
{
    Bytes file;
    put(file, "fLaC");
    file.push_back(0);
    putBe(file, 34, 3);
    Bytes stream(34, 0);
    // 44100 Hz, 2 channels, 24 bits, 441000 samples.
    const uint64_t packed = (44100ULL << 44) | (1ULL << 41) | (23ULL << 36) | 441000;
    for (int index = 0; index < 8; ++index) {
        stream[10 + index] = static_cast<uint8_t>(packed >> (56 - index * 8));
    }
    put(file, stream);
    const Bytes comment = vorbisComment({"TITLE=Lossless", "DISCNUMBER=2/3"});
    file.push_back(4);
    putBe(file, comment.size(), 3);
    put(file, comment);
    const Bytes back = flacPicture(4, picture(0x66));
    file.push_back(6);
    putBe(file, back.size(), 3);
    put(file, back);
    const Bytes front = flacPicture(3, picture(0x77));
    file.push_back(0x86);
    putBe(file, front.size(), 3);
    put(file, front);
    put(file, Bytes(1000, 0));

    audiometa::Info info;
    CHECK(readBytes(file, audiometa::Format::Flac, info));
    CHECK_EQ(info.sampleRate, 44100u);
    CHECK_EQ(info.channels, 2);
    CHECK_EQ(info.bits, 24);
    CHECK_EQ(info.milliseconds, static_cast<uint64_t>(10000));
    CHECK_EQ(info.title, std::string("Lossless"));
    CHECK_EQ(audiometa::trackText(info), std::string("Disc 2 of 3"));
    CHECK(info.cover == picture(0x77));
    CHECK_EQ(audiometa::formatText(info), std::string("FLAC, 24-bit, 44.1 kHz"));
    survivesDamage(file, audiometa::Format::Flac);
}

// --- WAV, MP4 ----------------------------------------------------------------

TEST("audio meta: WAV format, INFO list and length")
{
    Bytes chunks;
    put(chunks, "WAVE");
    put(chunks, "fmt ");
    putLe(chunks, 16, 4);
    putLe(chunks, 1, 2);
    putLe(chunks, 2, 2);
    putLe(chunks, 44100, 4);
    putLe(chunks, 176400, 4);
    putLe(chunks, 4, 2);
    putLe(chunks, 16, 2);
    Bytes list;
    put(list, "INFO");
    put(list, "INAM");
    putLe(list, 5, 4);
    put(list, "Take");
    list.push_back(0);
    list.push_back(0); // pad to even
    put(list, "IART");
    putLe(list, 4, 4);
    put(list, "Duo");
    list.push_back(0);
    put(chunks, "LIST");
    putLe(chunks, list.size(), 4);
    put(chunks, list);
    put(chunks, "data");
    putLe(chunks, 88200, 4);
    chunks.resize(chunks.size() + 88200, 0);
    Bytes file;
    put(file, "RIFF");
    putLe(file, chunks.size(), 4);
    put(file, chunks);

    audiometa::Info info;
    CHECK(readBytes(file, audiometa::Format::Wav, info));
    CHECK_EQ(info.title, std::string("Take"));
    CHECK_EQ(info.artist, std::string("Duo"));
    CHECK_EQ(info.milliseconds, static_cast<uint64_t>(500));
    CHECK_EQ(audiometa::formatText(info), std::string("WAV, 16-bit, 44.1 kHz"));
    survivesDamage(Bytes(file.begin(), file.begin() + 200), audiometa::Format::Wav);
}

namespace {

Bytes box(const std::string &type, const Bytes &body)
{
    Bytes out;
    putBe(out, body.size() + 8, 4);
    put(out, type);
    put(out, body);
    return out;
}

Bytes dataBox(uint32_t type, const Bytes &value)
{
    Bytes body;
    putBe(body, type, 4);
    putBe(body, 0, 4);
    put(body, value);
    return box("data", body);
}

Bytes textItem(const std::string &type, const std::string &value)
{
    Bytes bytes(value.begin(), value.end());
    return box(type, dataBox(1, bytes));
}

Bytes mp4File(bool alac)
{
    // Version 0 headers: the time scale at 12 and the duration at 16. The
    // movie says 9 s and the track 10 s; the track wins.
    Bytes mvhd(12, 0);
    putBe(mvhd, 1000, 4);
    putBe(mvhd, 9000, 4);
    mvhd.resize(100, 0);
    Bytes mdhd(12, 0);
    putBe(mdhd, 44100, 4);
    putBe(mdhd, 441000, 4);
    mdhd.resize(24, 0);
    Bytes hdlr(24, 0);
    std::memcpy(hdlr.data() + 8, "soun", 4);
    Bytes entry;
    putBe(entry, 0, 6);
    putBe(entry, 1, 2);
    putBe(entry, 0, 8);
    putBe(entry, 2, 2);  // channels
    putBe(entry, 16, 2); // sample size
    putBe(entry, 0, 4);
    putBe(entry, 44100u << 16, 4);
    if (alac) {
        Bytes cookie;
        putBe(cookie, 0, 4);    // version and flags
        putBe(cookie, 4096, 4); // frame length
        cookie.push_back(0);
        cookie.push_back(24); // bit depth
        cookie.push_back(40);
        cookie.push_back(10);
        cookie.push_back(14);
        cookie.push_back(2); // channels
        putBe(cookie, 255, 2);
        putBe(cookie, 0, 4);
        putBe(cookie, 0, 4);
        putBe(cookie, 96000, 4);
        put(entry, box("alac", cookie));
    }
    Bytes stsd;
    putBe(stsd, 0, 4);
    putBe(stsd, 1, 4);
    put(stsd, box(alac ? "alac" : "mp4a", entry));
    const Bytes trak = box(
        "trak", box("mdia", [&]() {
            Bytes mdia = box("mdhd", mdhd);
            put(mdia, box("hdlr", hdlr));
            put(mdia, box("minf", box("stbl", box("stsd", stsd))));
            return mdia;
        }()));
    Bytes ilst;
    put(ilst, textItem("\xa9nam", "Track Name"));
    put(ilst, textItem("\xa9" "ART", "Singer"));
    put(ilst, textItem("\xa9" "day", "2015-03-01T08:00:00Z"));
    Bytes trkn = {0, 0, 0, 5, 0, 10, 0, 0};
    put(ilst, box("trkn", dataBox(0, trkn)));
    put(ilst, box("gnre", dataBox(0, {0, 18})));
    put(ilst, box("covr", dataBox(13, picture(0x88))));
    Bytes meta;
    putBe(meta, 0, 4);
    put(meta, box("hdlr", Bytes(25, 0)));
    put(meta, box("ilst", ilst));
    Bytes moov = box("mvhd", mvhd);
    put(moov, trak);
    put(moov, box("udta", box("meta", meta)));
    Bytes file = box("ftyp", Bytes(16, 0));
    put(file, box("mdat", Bytes(160000, 0)));
    put(file, box("moov", moov));
    return file;
}

} // namespace

TEST("audio meta: MP4 items, cover, codec and length")
{
    const Bytes file = mp4File(false);
    audiometa::Info info;
    CHECK(readBytes(file, audiometa::Format::Mp4, info));
    CHECK_EQ(info.title, std::string("Track Name"));
    CHECK_EQ(info.artist, std::string("Singer"));
    CHECK_EQ(info.year, std::string("2015"));
    CHECK_EQ(info.genre, std::string("Rock"));
    CHECK_EQ(info.track, 5);
    CHECK_EQ(info.tracks, 10);
    CHECK(info.cover == picture(0x88));
    CHECK_EQ(info.codec, std::string("AAC"));
    CHECK_EQ(info.milliseconds, static_cast<uint64_t>(10000));
    CHECK_EQ(info.kilobits, 128u);
    CHECK_EQ(audiometa::formatText(info), std::string("AAC, 128 kbps, 44.1 kHz"));

    CHECK(readBytes(mp4File(true), audiometa::Format::Mp4, info));
    CHECK_EQ(audiometa::formatText(info), std::string("ALAC, 24-bit, 96 kHz"));

    // The same file with a small mdat, so damaging it reaches the boxes.
    Bytes rebuilt = box("ftyp", Bytes(16, 0));
    put(rebuilt, box("mdat", Bytes(100, 0)));
    put(rebuilt, Bytes(file.begin() + 24 + 8 + 160000, file.end()));
    survivesDamage(rebuilt, audiometa::Format::Mp4);
}

// --- 3DS formats, modules ------------------------------------------------------

namespace {

Bytes ctrHeader(const char *magic, uint16_t infoType, uint32_t infoAt)
{
    Bytes out;
    put(out, magic);
    putLe(out, 0xfeff, 2);
    putLe(out, 0x40, 2);
    putLe(out, 0x02000000, 4);
    putLe(out, 0, 4);
    putLe(out, 1, 2);
    putLe(out, 0, 2);
    putLe(out, infoType, 2);
    putLe(out, 0, 2);
    putLe(out, infoAt, 4);
    putLe(out, 0x100, 4);
    out.resize(0x40, 0);
    return out;
}

} // namespace

TEST("audio meta: BCSTM and BCWAV encoding, loop and length")
{
    Bytes stream = ctrHeader("CSTM", 0x4000, 0x40);
    put(stream, "INFO");
    putLe(stream, 0x100, 4);
    putLe(stream, 0x4100, 2);
    putLe(stream, 0, 2);
    putLe(stream, 0x18, 4); // stream info at INFO + 8 + 0x18
    putLe(stream, 0, 16);
    stream.push_back(2); // DSP-ADPCM
    stream.push_back(1); // loops
    stream.push_back(2); // channels
    stream.push_back(0);
    putLe(stream, 32728, 4);
    putLe(stream, 32728 * 12, 4);
    putLe(stream, 32728 * 165, 4);
    stream.resize(0x200, 0);

    audiometa::Info info;
    CHECK(readBytes(stream, audiometa::Format::Bcstm, info));
    CHECK_EQ(audiometa::formatText(info), std::string("BCSTM, DSP-ADPCM, 32728 Hz"));
    CHECK_EQ(audiometa::length(info.milliseconds), std::string("2:45"));
    CHECK_EQ(audiometa::loopText(info), std::string("0:12 to 2:45"));
    survivesDamage(stream, audiometa::Format::Bcstm);

    Bytes sound = ctrHeader("CWAV", 0x7000, 0x40);
    put(sound, "INFO");
    putLe(sound, 0x100, 4);
    sound.push_back(1); // PCM16
    sound.push_back(0);
    putLe(sound, 0, 2);
    putLe(sound, 22050, 4);
    putLe(sound, 0, 4);
    putLe(sound, 22050 / 2, 4);
    putLe(sound, 0, 4);
    putLe(sound, 1, 4); // one channel
    sound.resize(0x200, 0);
    CHECK(readBytes(sound, audiometa::Format::Bcwav, info));
    CHECK_EQ(audiometa::formatText(info), std::string("BCWAV, 16-bit, 22050 Hz, mono"));
    CHECK_EQ(audiometa::length(info.milliseconds), std::string("0.5s"));
    CHECK(!info.loops);
    survivesDamage(sound, audiometa::Format::Bcwav);
}

TEST("audio meta: tracker module titles and channels")
{
    Bytes mod(1084 + 100, 0);
    std::memcpy(mod.data(), "space debris", 12);
    std::memcpy(mod.data() + 1080, "8CHN", 4);
    audiometa::Info info;
    CHECK(readBytes(mod, audiometa::Format::Mod, info));
    CHECK_EQ(info.title, std::string("space debris"));
    CHECK_EQ(audiometa::formatText(info), std::string("MOD, 8 channels"));

    Bytes xm(400, 0);
    std::memcpy(xm.data(), "Extended Module: Strobe", 23);
    xm[68] = 12;
    CHECK(readBytes(xm, audiometa::Format::Xm, info));
    CHECK_EQ(info.title, std::string("Strobe"));
    CHECK_EQ(info.channels, 12);

    Bytes s3m(400, 0xff);
    std::memcpy(s3m.data(), "2nd Reality", 11);
    std::fill(s3m.begin() + 11, s3m.begin() + 28, 0);
    std::memcpy(s3m.data() + 44, "SCRM", 4);
    for (int channel = 0; channel < 6; ++channel) {
        s3m[64 + channel] = static_cast<uint8_t>(channel);
    }
    CHECK(readBytes(s3m, audiometa::Format::S3m, info));
    CHECK_EQ(info.title, std::string("2nd Reality"));
    CHECK_EQ(info.channels, 6);

    Bytes it(400, 0);
    std::memcpy(it.data(), "IMPMTitle", 9);
    CHECK(readBytes(it, audiometa::Format::It, info));
    CHECK_EQ(info.title, std::string("Title"));
    CHECK(!readBytes(Bytes(400, 0), audiometa::Format::It, info));
}

// --- Text ----------------------------------------------------------------------

TEST("audio meta: formats by name and text for the top screen")
{
    CHECK(audiometa::formatOf("a.MP3") == audiometa::Format::Mp3);
    CHECK(audiometa::formatOf("a.opus") == audiometa::Format::Ogg);
    CHECK(audiometa::formatOf("theme.bcstm") == audiometa::Format::Bcstm);
    CHECK(audiometa::formatOf("song") == audiometa::Format::Unknown);
    CHECK(audiometa::formatOf("song.mid") == audiometa::Format::Unknown);

    CHECK_EQ(audiometa::length(0), std::string(""));
    CHECK_EQ(audiometa::length(420), std::string("0.4s"));
    CHECK_EQ(audiometa::length(888), std::string("0.9s"));
    CHECK_EQ(audiometa::length(9940), std::string("9.9s"));
    CHECK_EQ(audiometa::length(10000), std::string("0:10"));
    CHECK_EQ(audiometa::length(185400), std::string("3:05"));
    CHECK_EQ(audiometa::length(3723000), std::string("1:02:03"));

    audiometa::Info info;
    CHECK_EQ(audiometa::formatText(info), std::string(""));
    CHECK_EQ(audiometa::trackText(info), std::string(""));
    CHECK_EQ(audiometa::albumText(info), std::string(""));
    info.track = 4;
    CHECK_EQ(audiometa::trackText(info), std::string("4"));
    info.disc = 1;
    info.discs = 1;
    CHECK_EQ(audiometa::trackText(info), std::string("4"));
    info.year = "1999";
    CHECK_EQ(audiometa::albumText(info), std::string("1999"));
    info.codec = "WAV";
    info.encoding = "IMA-ADPCM";
    info.sampleRate = 48000;
    info.channels = 6;
    CHECK_EQ(audiometa::formatText(info),
             std::string("WAV, IMA-ADPCM, 48 kHz, 6 channels"));

    CHECK(audiometa::coverRank("cover.jpg") > audiometa::coverRank("folder.jpg"));
    CHECK(audiometa::coverRank("Folder.JPG") > audiometa::coverRank("AlbumArt_{X}_Large.jpg"));
    CHECK(audiometa::coverRank("AlbumArt_{X}_Large.jpg") >
          audiometa::coverRank("AlbumArtSmall.jpg"));
    CHECK(audiometa::coverRank("AlbumArtSmall.jpg") > 0);
    CHECK_EQ(audiometa::coverRank("cover.gif"), 0);
    CHECK_EQ(audiometa::coverRank("._cover.jpg"), 0);
    CHECK_EQ(audiometa::coverRank("holiday.png"), 0);
}
