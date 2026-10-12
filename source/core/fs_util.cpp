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

} // namespace fs
