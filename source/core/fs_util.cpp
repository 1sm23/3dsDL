#include "fs_util.hpp"

#include "text.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace fs {

bool fileExists(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool directoryExists(const std::string &path)
{
    DIR *directory = opendir(path.c_str());
    if (directory == nullptr) {
        return false;
    }
    closedir(directory);
    return true;
}

int64_t fileSize(const std::string &path)
{
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
        return -1;
    }
    return static_cast<int64_t>(info.st_size);
}

bool makeDirectories(const std::string &folder)
{
    if (folder.empty()) {
        return false;
    }
    if (directoryExists(folder)) {
        return true;
    }

    // Skip a "device:/" prefix such as sdmc:/ and the root slash.
    size_t position = folder.find(":/");
    position = position == std::string::npos ? 0 : position + 2;
    while (position < folder.size() && folder[position] == '/') {
        ++position;
    }

    while (position <= folder.size()) {
        const size_t slash = folder.find('/', position);
        const size_t end = slash == std::string::npos ? folder.size() : slash;
        if (end > position) {
            const std::string partial = folder.substr(0, end);
            if (!directoryExists(partial) &&
                mkdir(partial.c_str(), 0777) != 0 && errno != EEXIST) {
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        position = slash + 1;
    }
    return directoryExists(folder);
}

bool readFile(const std::string &path, std::string &contents)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    contents.clear();
    char buffer[4096];
    size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        contents.append(buffer, count);
    }
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok;
}

bool writeFile(const std::string &path, const std::string &contents)
{
    const std::string temporary = path + ".tmp";
    FILE *file = std::fopen(temporary.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const bool written =
        std::fwrite(contents.data(), 1, contents.size(), file) ==
        contents.size();
    if (std::fclose(file) != 0 || !written) {
        std::remove(temporary.c_str());
        return false;
    }

    // FAT renames fail when the target exists, so the old file goes first.
    std::remove(path.c_str());
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

bool removeFile(const std::string &path)
{
    return std::remove(path.c_str()) == 0;
}

bool renameFile(const std::string &from, const std::string &to)
{
    return std::rename(from.c_str(), to.c_str()) == 0;
}

std::vector<std::string> listFolders(const std::string &folder)
{
    std::vector<std::string> names;
    DIR *directory = opendir(folder.c_str());
    if (directory == nullptr) {
        return names;
    }

    std::string base = folder;
    if (!base.empty() && base.back() != '/') {
        base += '/';
    }

    while (dirent *entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.') {
            continue;
        }
        bool isFolder = false;
#ifdef DT_DIR
        if (entry->d_type == DT_DIR) {
            isFolder = true;
        } else if (entry->d_type == DT_UNKNOWN) {
            isFolder = directoryExists(base + name);
        }
#else
        isFolder = directoryExists(base + name);
#endif
        if (isFolder) {
            names.push_back(name);
        }
    }
    closedir(directory);

    std::sort(names.begin(), names.end(),
              [](const std::string &a, const std::string &b) {
                  return text::lower(a) < text::lower(b);
              });
    return names;
}

std::vector<Entry> listEntries(const std::string &folder, bool *ok,
                               bool hidden)
{
    std::vector<Entry> entries;
    DIR *directory = opendir(folder.c_str());
    if (ok != nullptr) {
        *ok = directory != nullptr;
    }
    if (directory == nullptr) {
        return entries;
    }

    std::string base = folder;
    if (!base.empty() && base.back() != '/') {
        base += '/';
    }

    while (dirent *item = readdir(directory)) {
        Entry entry;
        entry.name = item->d_name;
        if (entry.name.empty() || entry.name == "." || entry.name == ".." ||
            (entry.name[0] == '.' && !hidden)) {
            continue;
        }
        struct stat info {};
        if (stat((base + entry.name).c_str(), &info) == 0) {
            entry.folder = S_ISDIR(info.st_mode);
            entry.modified = static_cast<int64_t>(info.st_mtime);
            if (!entry.folder) {
                entry.size = static_cast<int64_t>(info.st_size);
            }
        }
        entries.push_back(entry);
    }
    closedir(directory);

    sortEntries(entries, SortOrder::Name);
    return entries;
}

void sortEntries(std::vector<Entry> &entries, SortOrder order)
{
    const auto extensionOf = [](const std::string &name) {
        const size_t dot = name.find_last_of('.');
        return dot == std::string::npos || dot == 0
            ? std::string()
            : text::lower(name.substr(dot + 1));
    };
    std::stable_sort(
        entries.begin(), entries.end(), [&](const Entry &a, const Entry &b) {
            if (a.folder != b.folder) {
                return a.folder;
            }
            const std::string nameA = text::lower(a.name);
            const std::string nameB = text::lower(b.name);
            switch (order) {
            case SortOrder::Size:
                if (!a.folder && a.size != b.size) {
                    return a.size > b.size;
                }
                break;
            case SortOrder::Date:
                if (a.modified != b.modified) {
                    return a.modified > b.modified;
                }
                break;
            case SortOrder::Type: {
                const std::string typeA = extensionOf(a.name);
                const std::string typeB = extensionOf(b.name);
                if (!a.folder && typeA != typeB) {
                    return typeA < typeB;
                }
                break;
            }
            case SortOrder::Name:
                break;
            }
            return nameA != nameB ? nameA < nameB : a.name < b.name;
        });
}

} // namespace fs
