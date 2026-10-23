#include "file_ops.hpp"

#include "fs_util.hpp"
#include "text.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fileops {
namespace {

// Large enough that the SD card sees few requests, small enough to fit
// comfortably next to everything else on an Old 3DS.
constexpr size_t CopyChunk = 256 * 1024;

std::string join(const std::string &folder, const std::string &entry)
{
    return folder.empty() || folder.back() == '/' ? folder + entry
                                                  : folder + "/" + entry;
}

// Names in folder, without "." and "..".
bool children(const std::string &folder, std::vector<std::string> &names)
{
    DIR *directory = opendir(folder.c_str());
    if (directory == nullptr) {
        return false;
    }
    while (dirent *item = readdir(directory)) {
        const std::string entry = item->d_name;
        if (entry != "." && entry != "..") {
            names.push_back(entry);
        }
    }
    closedir(directory);
    return true;
}

std::string failure(const std::string &what, const std::string &path)
{
    const int code = errno;
    std::string message = what + " " + name(path) + ".";
    if (code == ENOSPC) {
        message += " The SD card is full.";
    } else if (code != 0) {
        message += std::string(" ") + std::strerror(code) + ".";
    }
    return message;
}

Result copyFile(const std::string &source, const std::string &destination,
                Observer &observer, uint64_t &done, uint64_t total)
{
    Result result;
    std::FILE *input = std::fopen(source.c_str(), "rb");
    if (input == nullptr) {
        result.status = Status::Failed;
        result.message = failure("Could not open", source);
        return result;
    }
    std::FILE *output = std::fopen(destination.c_str(), "wb");
    if (output == nullptr) {
        result.status = Status::Failed;
        result.message = failure("Could not create", destination);
        std::fclose(input);
        return result;
    }

    std::unique_ptr<char[]> buffer(new char[CopyChunk]);
    const std::string current = name(source);
    observer.onProgress(done, total, current);
    while (true) {
        if (observer.cancelRequested()) {
            result.status = Status::Cancelled;
            break;
        }
        const size_t read = std::fread(buffer.get(), 1, CopyChunk, input);
        if (read == 0) {
            if (std::ferror(input)) {
                result.status = Status::Failed;
                result.message = failure("Could not read", source);
            }
            break;
        }
        if (std::fwrite(buffer.get(), 1, read, output) != read) {
            result.status = Status::Failed;
            result.message = failure("Could not write", destination);
            break;
        }
        done += read;
        observer.onProgress(done, total, current);
    }

    std::fclose(input);
    if (std::fclose(output) != 0 && result.status == Status::Ok) {
        result.status = Status::Failed;
        result.message = failure("Could not finish writing", destination);
    }
    if (result.status != Status::Ok) {
        std::remove(destination.c_str());
    } else {
        result.files = 1;
    }
    return result;
}

void measureInto(const std::string &path, Totals &totals,
                 const std::atomic<bool> *stop)
{
    if (stop != nullptr && stop->load()) {
        return;
    }
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) {
        return;
    }
    if (!S_ISDIR(info.st_mode)) {
        ++totals.files;
        totals.bytes += static_cast<uint64_t>(info.st_size);
        return;
    }
    std::vector<std::string> names;
    children(path, names);
    for (const std::string &entry : names) {
        const std::string child = join(path, entry);
        if (isFolder(child)) {
            ++totals.folders;
        }
        measureInto(child, totals, stop);
    }
}

} // namespace

Totals measure(const std::string &path, const std::atomic<bool> *stop)
{
    Totals totals;
    measureInto(path, totals, stop);
    return totals;
}

Result copy(const std::string &source, const std::string &destination,
            Observer &observer, uint64_t &done, uint64_t total)
{
    Result result;
    if (exists(destination)) {
        result.status = Status::Failed;
        result.message = name(destination) + " already exists.";
        return result;
    }
    if (!isFolder(source)) {
        return copyFile(source, destination, observer, done, total);
    }
    if (isInside(destination, source)) {
        result.status = Status::Failed;
        result.message = "A folder cannot be copied into itself.";
        return result;
    }
    if (mkdir(destination.c_str(), 0777) != 0) {
        result.status = Status::Failed;
        result.message = failure("Could not create the folder", destination);
        return result;
    }
    std::vector<std::string> names;
    if (!children(source, names)) {
        result.status = Status::Failed;
        result.message = failure("Could not open the folder", source);
        return result;
    }
    for (const std::string &entry : names) {
        const Result part = copy(join(source, entry),
                                 join(destination, entry), observer, done,
                                 total);
        result.files += part.files;
        if (part.status != Status::Ok) {
            result.status = part.status;
            result.message = part.message;
            return result;
        }
    }
    return result;
}

Result remove(const std::string &path, Observer &observer, uint64_t &done,
              uint64_t total)
{
    Result result;
    if (observer.cancelRequested()) {
        result.status = Status::Cancelled;
        return result;
    }
    if (isFolder(path)) {
        std::vector<std::string> names;
        if (!children(path, names)) {
            result.status = Status::Failed;
            result.message = failure("Could not open the folder", path);
            return result;
        }
        for (const std::string &entry : names) {
            const Result part =
                remove(join(path, entry), observer, done, total);
            result.files += part.files;
            if (part.status != Status::Ok) {
                result.status = part.status;
                result.message = part.message;
                return result;
            }
        }
        if (rmdir(path.c_str()) != 0) {
            result.status = Status::Failed;
            result.message = failure("Could not delete the folder", path);
        }
        return result;
    }
    observer.onProgress(done, total, name(path));
    if (std::remove(path.c_str()) != 0) {
        result.status = Status::Failed;
        result.message = failure("Could not delete", path);
        return result;
    }
    result.files = 1;
    ++done;
    observer.onProgress(done, total, name(path));
    return result;
}

bool move(const std::string &source, const std::string &destination,
          std::string &error)
{
    if (exists(destination)) {
        error = name(destination) + " already exists.";
        return false;
    }
    if (isFolder(source) && isInside(destination, source)) {
        error = "A folder cannot be moved into itself.";
        return false;
    }
    if (std::rename(source.c_str(), destination.c_str()) != 0) {
        error = failure("Could not move", source);
        return false;
    }
    return true;
}

bool isFolder(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool exists(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0;
}

bool isInside(const std::string &path, const std::string &folder)
{
    std::string base = folder;
    while (base.size() > 1 && base.back() == '/') {
        base.pop_back();
    }
    return path == base ||
           (path.size() > base.size() && path.compare(0, base.size(), base) == 0 &&
            path[base.size()] == '/');
}

std::string name(const std::string &path)
{
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed.substr(trimmed.find_last_of('/') + 1);
}

bool validName(const std::string &value, std::string &error)
{
    const std::string trimmed = text::trim(value);
    if (trimmed.empty()) {
        error = "The name is empty.";
        return false;
    }
    if (trimmed == "." || trimmed == "..") {
        error = "\".\" and \"..\" cannot be used as names.";
        return false;
    }
    if (trimmed.find_first_of("\\/:*?\"<>|") != std::string::npos) {
        error = "Names cannot contain \\ / : * ? \" < > or |.";
        return false;
    }
    for (const char character : trimmed) {
        if (static_cast<unsigned char>(character) < 0x20) {
            error = "Names cannot contain control characters.";
            return false;
        }
    }
    if (trimmed.size() > 255) {
        error = "The name is too long.";
        return false;
    }
    if (trimmed.back() == '.') {
        error = "Names cannot end with a dot.";
        return false;
    }
    return true;
}

int64_t modifiedTime(const std::string &path)
{
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) {
        return 0;
    }
    return fs::modifiedTime(path, static_cast<int64_t>(info.st_mtime));
}

} // namespace fileops
