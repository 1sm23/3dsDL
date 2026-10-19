#pragma once

#include "cia.hpp"
#include "file_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

// The name, publisher, title ID and icon inside game and homebrew files,
// for the top screen in Files. CIAs have their own, fuller reader in cia.hpp.
namespace titlemeta {

struct Meta {
    std::string name;
    std::string publisher;
    std::string productCode; // "CTR-P-ABCD" or a DS game code
    uint64_t titleId = 0;
    bool hasTitleId = false;
    // 48x48 RGB565, row by row; empty when there is none.
    std::vector<uint16_t> icon;
    // Something worth saying, such as "Encrypted, so the icon is hidden."
    std::string note;
};

bool read(const cia::Reader &read, uint64_t size, filetype::Kind kind,
          Meta &meta, std::string &error);
bool read(const std::string &path, filetype::Kind kind, Meta &meta,
          std::string &error);

} // namespace titlemeta
