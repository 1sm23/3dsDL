#include "fs_util.hpp"

#include "text.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <functional>
#include <sys/stat.h>

#ifdef __3DS__
#include <3ds.h>
#endif

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

int64_t modifiedTime(const std::string &path, int64_t statTime)
{
    if (statTime != 0) {
        return statTime;
    }
#ifdef __3DS__
    u64 time = 0;
    if (R_SUCCEEDED(archive_getmtime(path.c_str(), &time))) {
        return static_cast<int64_t>(time);
    }
    return 0;
#else
    struct stat info {};
    return stat(path.c_str(), &info) == 0 ? static_cast<int64_t>(info.st_mtime)
                                          : 0;
#endif
}

#ifdef __3DS__
namespace {

// Lists an SD card folder ("sdmc:/a/b/") from the card's archive, which
// hands over each entry's size and kind in batches. stat() opens every file
// to find its size, and in a folder of hundreds of files each open searches
// the whole folder again, which took the 3DS minutes.
// A path on the SD card ("sdmc:/a/b") as the card's archive takes it.
class SdPath {
public:
    explicit SdPath(const std::string &path)
    {
        const std::string inside = path.substr(5); // after "sdmc:"
        units_.assign(inside.size() + 1, 0);
        const ssize_t count = utf8_to_utf16(
            units_.data(), reinterpret_cast<const uint8_t *>(inside.c_str()),
            inside.size());
        units_.resize(count >= 0 ? static_cast<size_t>(count) + 1 : 1);
        units_.back() = 0;
    }
    FS_Path get() const
    {
        return {PATH_UTF16, static_cast<u32>(units_.size() * 2), units_.data()};
    }

private:
    std::vector<uint16_t> units_;
};

bool listSdEntries(const std::string &folder, bool hidden, bool times,
                   const std::function<void(std::vector<Entry> &&)> &take,
                   const std::atomic<bool> *stop)
{
    const SdPath sdPath(folder);
    const FS_Path path = sdPath.get();

    FS_Archive archive = 0;
    if (R_FAILED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC,
                                    fsMakePath(PATH_EMPTY, "")))) {
        return false;
    }
    Handle directory = 0;
    if (R_FAILED(FSUSER_OpenDirectory(&directory, archive, path))) {
        FSUSER_CloseArchive(archive);
        return false;
    }

    std::string base = folder;
    if (base.back() != '/') {
        base += '/';
    }
    std::vector<FS_DirectoryEntry> batch(32);
    while (stop == nullptr || !stop->load()) {
        std::vector<Entry> entries;
        u32 read = 0;
        if (R_FAILED(FSDIR_Read(directory, &read,
                                static_cast<u32>(batch.size()), batch.data())) ||
            read == 0) {
            break;
        }
        for (u32 index = 0; index < read; ++index) {
            const FS_DirectoryEntry &item = batch[index];
            size_t length = 0;
            while (length < 0x106 && item.name[length] != 0) {
                ++length;
            }
            std::string name(length * 3 + 1, '\0');
            const ssize_t bytes = utf16_to_utf8(
                reinterpret_cast<uint8_t *>(&name[0]), item.name, length);
            name.resize(bytes > 0 ? static_cast<size_t>(bytes) : 0);
            if (name.empty() || name == "." || name == ".." ||
                (name[0] == '.' && !hidden)) {
                continue;
            }
            Entry entry;
            entry.name = name;
            entry.folder = (item.attributes & FS_ATTRIBUTE_DIRECTORY) != 0;
            if (!entry.folder) {
                entry.size = static_cast<int64_t>(item.fileSize);
            }
            if (times) {
                entry.modified = modifiedTime(base + name);
            }
            entries.push_back(entry);
        }
        if (!entries.empty()) {
            take(std::move(entries));
        }
    }
    FSDIR_Close(directory);
    FSUSER_CloseArchive(archive);
    return true;
}

} // namespace
#endif

bool removeFolderTree(const std::string &folder)
{
#ifdef __3DS__
    if (folder.compare(0, 6, "sdmc:/") != 0) {
        return false;
    }
    std::string path = folder;
    while (path.size() > 6 && path.back() == '/') {
        path.pop_back();
    }
    FS_Archive archive = 0;
    if (R_FAILED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC,
                                    fsMakePath(PATH_EMPTY, "")))) {
        return false;
    }
    const SdPath sdPath(path);
    const bool removed =
        R_SUCCEEDED(FSUSER_DeleteDirectoryRecursively(archive, sdPath.get()));
    FSUSER_CloseArchive(archive);
    return removed;
#else
    (void)folder;
    return false;
#endif
}

bool readEntries(const std::string &folder, bool hidden, bool times,
                 const std::function<void(std::vector<Entry> &&)> &take,
                 const std::atomic<bool> *stop)
{
#ifdef __3DS__
    if (folder.compare(0, 6, "sdmc:/") == 0) {
        return listSdEntries(folder, hidden, times, take, stop);
    }
#endif
    DIR *directory = opendir(folder.c_str());
    if (directory == nullptr) {
        return false;
    }

    std::string base = folder;
    if (!base.empty() && base.back() != '/') {
        base += '/';
    }

    constexpr size_t BatchSize = 64;
    std::vector<Entry> entries;
    while (stop == nullptr || !stop->load()) {
        dirent *item = readdir(directory);
        if (item == nullptr) {
            break;
        }
        Entry entry;
        entry.name = item->d_name;
        if (entry.name.empty() || entry.name == "." || entry.name == ".." ||
            (entry.name[0] == '.' && !hidden)) {
            continue;
        }
        struct stat info {};
        if (stat((base + entry.name).c_str(), &info) == 0) {
            entry.folder = S_ISDIR(info.st_mode);
            if (times) {
                entry.modified = modifiedTime(
                    base + entry.name, static_cast<int64_t>(info.st_mtime));
            }
            if (!entry.folder) {
                entry.size = static_cast<int64_t>(info.st_size);
            }
        }
        entries.push_back(entry);
        if (entries.size() == BatchSize) {
            take(std::move(entries));
            entries.clear();
        }
    }
    closedir(directory);
    if (!entries.empty()) {
        take(std::move(entries));
    }
    return true;
}

std::vector<Entry> listEntries(const std::string &folder, bool *ok,
                               bool hidden, bool times)
{
    std::vector<Entry> entries;
    const bool listed = readEntries(
        folder, hidden, times, [&](std::vector<Entry> &&batch) {
            for (Entry &entry : batch) {
                entries.push_back(std::move(entry));
            }
        });
    if (ok != nullptr) {
        *ok = listed;
    }
    sortEntries(entries, SortOrder::Name);
    return entries;
}

bool entryBefore(const Entry &a, const Entry &b, SortOrder order)
{
    const auto extensionOf = [](const std::string &name) {
        const size_t dot = name.find_last_of('.');
        return dot == std::string::npos || dot == 0
            ? std::string()
            : text::lower(name.substr(dot + 1));
    };
    if (a.folder != b.folder) {
        return a.folder;
    }
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
    const std::string nameA = text::lower(a.name);
    const std::string nameB = text::lower(b.name);
    return nameA != nameB ? nameA < nameB : a.name < b.name;
}

void mergeEntries(std::vector<Entry> &sorted, std::vector<Entry> &&batch,
                  SortOrder order)
{
    const auto before = [order](const Entry &a, const Entry &b) {
        return entryBefore(a, b, order);
    };
    std::stable_sort(batch.begin(), batch.end(), before);
    const size_t middle = sorted.size();
    for (Entry &entry : batch) {
        sorted.push_back(std::move(entry));
    }
    std::inplace_merge(sorted.begin(), sorted.begin() + middle, sorted.end(),
                       before);
}

void sortEntries(std::vector<Entry> &entries, SortOrder order)
{
    std::stable_sort(entries.begin(), entries.end(),
                     [order](const Entry &a, const Entry &b) {
                         return entryBefore(a, b, order);
                     });
}

} // namespace fs
