#pragma once

#include "cia.hpp"

#include <cstdint>
#include <string>
#include <vector>

// What a song file says about itself, for Files: its tags, its cover, how
// long it is and how it is stored. Only headers and tags are read, never the
// audio, so it is quick enough for the highlighted row.
namespace audiometa {

enum class Format {
    Unknown,
    Mp3,
    Ogg, // Vorbis or Opus, told apart by the first packet
    Flac,
    Wav,
    Mp4, // .m4a, with AAC or ALAC inside
    Aac, // raw ADTS
    Bcstm,
    Bcwav,
    Mod,
    Xm,
    S3m,
    It
};

// By extension only, as filetype::fromName decides.
Format formatOf(const std::string &name);

struct Info {
    std::string title;
    std::string artist;
    std::string albumArtist;
    std::string album;
    std::string year;
    std::string genre;
    int track = 0;
    int tracks = 0;
    int disc = 0;
    int discs = 0;

    std::string codec;    // "MP3", "Opus", "AAC", "BCSTM"
    std::string encoding; // what the container holds, when it says: "DSP-ADPCM"
    uint32_t kilobits = 0; // a lossy format's average bit rate, per second
    int bits = 0;          // PCM and lossless formats' sample size
    uint32_t sampleRate = 0;
    int channels = 0;
    uint64_t milliseconds = 0; // 0 when the file does not say
    bool loops = false;        // 3DS streams and sounds
    uint64_t loopStart = 0;    // milliseconds
    uint64_t loopEnd = 0;

    // The front cover, or else the first picture, as JPEG or PNG bytes. Only
    // filled when asked for, and left out past MaxCover.
    std::vector<uint8_t> cover;
};

constexpr uint64_t MaxCover = 8 * 1024 * 1024;

// False when the file is not what its extension says. Missing tags are not
// an error.
bool read(const cia::Reader &read, uint64_t size, Format format, Info &info,
          bool withCover);
bool read(const std::string &path, Info &info, bool withCover);

// Text for the top screen; each is "" when there is nothing to say.
// "3:05", "1:02:03", or "0.4s" for short sounds.
std::string length(uint64_t milliseconds);
// "MP3, 320 kbps, 44.1 kHz", "FLAC, 24-bit, 96 kHz", "XM, 8 channels"
std::string formatText(const Info &info);
// "3 of 12", "3 of 12, disc 1 of 2"
std::string trackText(const Info &info);
// "Discovery (2001)", or the year alone
std::string albumText(const Info &info);
// "0:12 to 2:45"
std::string loopText(const Info &info);

// How surely a file name in a song's folder is the album's cover:
// "cover.jpg" and "folder.png" rank high, other pictures 0.
int coverRank(const std::string &name);

} // namespace audiometa
