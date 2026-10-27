#include "i18n.hpp"

#include "fs_util.hpp"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <utility>

namespace i18n {
namespace {

const std::vector<Language> Supported = {
    {"en", "English"},
    {"zh", "简体中文"},
    {"es", "Español"},
    {"fr", "Français"},
    {"pt", "Português"},
    {"de", "Deutsch"},
    {"ja", "日本語"},
};

struct Entry {
    std::string key;
    std::vector<std::pair<std::string, std::string>> values;
};

std::vector<Entry> catalog;
std::atomic<size_t> currentLanguage {0};
const std::vector<std::string> LanguageCodes = {"en", "zh", "es", "fr", "pt", "de", "ja"};

const json::Value *field(const json::Value &object, const std::string &key)
{
    if (object.type != json::Type::Object) {
        return nullptr;
    }
    for (const auto &candidate : object.fields) {
        if (candidate.first == key) {
            return &candidate.second;
        }
    }
    return nullptr;
}

const std::string *translated(const Entry &entry, const std::string &code)
{
    for (const auto &value : entry.values) {
        if (value.first == code) {
            return &value.second;
        }
    }
    return nullptr;
}

} // namespace

const std::vector<Language> &languages()
{
    return Supported;
}

const Language *findLanguage(const std::string &code)
{
    for (const Language &language : Supported) {
        if (code == language.code) {
            return &language;
        }
    }
    return nullptr;
}

bool init(const std::string &path)
{
    catalog.clear();

    std::string contents;
    if (!fs::readFile(path, contents)) {
        return false;
    }

    json::Value root;
    std::string error;
    if (!json::parse(contents, root, error) ||
        root.type != json::Type::Object) {
        return false;
    }

    for (const auto &item : root.fields) {
        if (item.second.type != json::Type::Object) {
            continue;
        }
        Entry entry;
        entry.key = item.first;
        for (const Language &language : Supported) {
            const json::Value *value = field(item.second, language.code);
            if (value != nullptr && value->type == json::Type::String) {
                entry.values.emplace_back(language.code, value->string);
            }
        }
        catalog.push_back(std::move(entry));
    }
    std::sort(catalog.begin(), catalog.end(),
              [](const Entry &left, const Entry &right) {
                  return left.key < right.key;
              });
    return true;
}

void setLanguage(const std::string &code)
{
    const auto found = std::find(LanguageCodes.begin(), LanguageCodes.end(), code);
    currentLanguage = found == LanguageCodes.end() ? 0 : found - LanguageCodes.begin();
}

const std::string &language()
{
    return LanguageCodes[currentLanguage.load()];
}

std::string languageName(const std::string &code)
{
    const Language *found = findLanguage(code);
    return found == nullptr ? code : found->name;
}

std::string t(const std::string &key)
{
    const auto entry = std::lower_bound(
        catalog.begin(), catalog.end(), key,
        [](const Entry &candidate, const std::string &value) {
            return candidate.key < value;
        });
    if (entry != catalog.end() && entry->key == key) {
        if (const std::string *value = translated(*entry, language())) {
            return *value;
        }
        if (const std::string *value = translated(*entry, "en")) {
            return *value;
        }
    }
    return key;
}

const char *c(const char *key)
{
    const auto entry = std::lower_bound(
        catalog.begin(), catalog.end(), key,
        [](const Entry &candidate, const char *value) {
            return candidate.key < value;
        });
    if (entry != catalog.end() && entry->key == key) {
        if (const std::string *value = translated(*entry, language())) {
            return value->c_str();
        }
        if (const std::string *value = translated(*entry, "en")) {
            return value->c_str();
        }
    }
    return key;
}

std::string t(const std::string &key,
              std::initializer_list<std::string> args)
{
    const std::string format = t(key);
    std::string result;
    for (size_t position = 0; position < format.size();) {
        if (format[position] == '{') {
            size_t end = position + 1;
            size_t index = 0;
            while (end < format.size() && format[end] >= '0' && format[end] <= '9') {
                index = std::min(index * 10 + static_cast<size_t>(format[end] - '0'), args.size());
                ++end;
            }
            if (end > position + 1 && end < format.size() && format[end] == '}' && index < args.size()) {
                result += *(args.begin() + index);
                position = end + 1;
                continue;
            }
        }
        result += format[position++];
    }
    return result;
}

std::string count(uint64_t value, const std::string &word,
                  const std::string &formatted)
{
    const bool singular = value == 1 || (language() == "fr" && value == 0);
    const std::string noun = singular ? word : word == "entry" ? "entries" : word + "s";
    return t("{0} " + noun, {formatted});
}

} // namespace i18n
