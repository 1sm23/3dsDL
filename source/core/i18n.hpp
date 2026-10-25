#pragma once

#include <initializer_list>
#include <string>
#include <vector>

// Runtime localization for the 3DS UI. The catalog is loaded from ROMFS once
// and looked up by stable key, following the same model as Checkpoint.
namespace i18n {

struct Language {
    const char *code;
    const char *name;
};

// The names are shown in their own language so the setting remains usable
// after changing the UI language.
const std::vector<Language> &languages();
const Language *findLanguage(const std::string &code);

bool init(const std::string &path);
void setLanguage(const std::string &code);
const std::string &language();
std::string languageName(const std::string &code);

// Lookup falls back to English, then to the key itself so a missing resource
// is visible during development rather than silently becoming empty text.
std::string t(const std::string &key);
std::string t(const std::string &key,
              std::initializer_list<std::string> args);

} // namespace i18n
