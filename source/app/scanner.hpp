#pragma once

#include <string>

namespace scanner {

// Opens the outer camera and scans until a QR code holding an http(s) link is
// found. Returns the link, or "" if the user cancelled or the camera failed
// (in which case an error has already been shown). typeInstead is set when
// the user chose to type a link instead.
std::string scan(bool *typeInstead = nullptr);

} // namespace scanner
