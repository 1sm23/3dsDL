#include "i18n.hpp"

#include "fs_util.hpp"
#include "json.hpp"

#include <algorithm>
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
std::string currentLanguage = "en";

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
    currentLanguage = findLanguage(code) == nullptr ? "en" : code;
}

const std::string &language()
{
    return currentLanguage;
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
        if (const std::string *value = translated(*entry, currentLanguage)) {
            return *value;
        }
        if (const std::string *value = translated(*entry, "en")) {
            return *value;
        }
    }
    return key;
}

std::string t(const std::string &key,
              std::initializer_list<std::string> args)
{
    std::string result = t(key);
    size_t index = 0;
    for (const std::string &argument : args) {
        const std::string token = "{" + std::to_string(index++) + "}";
        size_t position = 0;
        while ((position = result.find(token, position)) != std::string::npos) {
            result.replace(position, token.size(), argument);
            position += argument.size();
        }
    }
    return result;
}

} // namespace i18n
