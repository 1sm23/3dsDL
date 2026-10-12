#pragma once

#include <string>

namespace scanner {

// Opens the outer camera and scans until a QR code holding an http(s) link is
// found. Returns the link, or "" if the user cancelled or the camera failed
// (in which case an error has already been shown).
std::string scan();

} // namespace scanner
