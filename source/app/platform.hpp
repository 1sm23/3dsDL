#pragma once

#include <cstdint>
#include <string>

// Console services that the screens need, kept in one place.
namespace platform {

enum class Wifi { Connected, Disconnected, Unknown };

Wifi wifiStatus();
// Free space on the SD card in bytes, or -1 when it cannot be read.
int64_t sdFreeBytes();
// The SD card's cluster size: the least space any file takes. 0 when unknown.
uint64_t sdClusterSize();
// The SD card's size in bytes, or -1 when it cannot be read.
int64_t sdTotalBytes();
// Tells one SD card from another: its serial (CID) where the console gives
// it, and its size; 0 when neither can be read.
uint64_t sdCardId();
// Seconds since 1970 by the console clock (which has no time zone).
int64_t now();

// The model as the CFG service numbers it (CFG_SystemModel), or -1 when it
// cannot be read.
int systemModel();
// The system version as System Settings shows it, "11.17.0-50U", or "" when
// it cannot be read.
std::string systemVersion();

// Sets the console clock to localUnixMilliseconds (local time, since 1970),
// the same way Luma3DS's Rosalina menu does. Fails with an explanation when
// 3dsDL is not allowed to change the clock.
bool setConsoleClock(int64_t localUnixMilliseconds, std::string &error);

// Text allows several lines and an empty result, for editing files.
enum class KeyboardKind { Url, Path, Name, Text };
// Shows the software keyboard. Returns false when the user cancels.
bool keyboard(KeyboardKind kind, const std::string &hint,
              const std::string &initial, std::string &output);

} // namespace platform
