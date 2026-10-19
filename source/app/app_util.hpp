#pragma once

#include "cia.hpp"
#include "ui.hpp"

#include <cstdint>
#include <string>

// Small pieces of screen code shared by app.cpp and files.cpp.
namespace screens {

std::string basename(const std::string &path);
std::string plural(uint64_t count, const std::string &word);

// A panel on the top screen with a small label above a wrapped value.
void drawLabelledPanel(float y, float height, const std::string &label,
                       const std::string &value, u32 valueColor);

// The title's name from its icon data, or its title ID when it has none.
std::string ciaName(const cia::Info &info);
// Publisher, kind, version, size and the installed version, if any.
void addCiaParagraphs(ui::Dialog &dialog, const cia::Info &info);

} // namespace screens
