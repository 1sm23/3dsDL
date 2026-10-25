#pragma once

#include <string>

// What kind of file a name looks like, so Files can pick a colour, a preview
// and the options that make sense for it.
namespace filetype {

enum class Kind {
    Folder,
    Cia,
    Ticket,
    Homebrew,       // .3dsx
    Smdh,           // a bare icon and title block
    CartridgeImage, // .3ds and .cci
    Nds,            // DS and DSi games
    Firm,
    Image,
    Text,
    Archive, // .zip, .7z, .rar, .tar and compressed tars
    Audio,
    Video,
    Other
};

// Decides by extension only, so it is cheap enough for a whole folder.
Kind fromName(const std::string &name);
const char *label(Kind kind);

// Kinds whose top screen shows a title, publisher and icon.
bool hasTitleMeta(Kind kind);

} // namespace filetype
