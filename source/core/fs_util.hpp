#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Thin wrappers over the C library so the same code works on the SD card
// (through libctru's sdmc: device) and on a desktop file system in tests.
namespace fs {

bool fileExists(const std::string &path);
bool directoryExists(const std::string &path);
// File size in bytes, or -1 if the file cannot be inspected.
int64_t fileSize(const std::string &path);

// Creates folder and any missing parents. "sdmc:/" style device prefixes are
// left alone. Succeeds if the folder already exists.
bool makeDirectories(const std::string &folder);

bool readFile(const std::string &path, std::string &contents);
// Writes through a temporary file so a failed write never truncates path.
bool writeFile(const std::string &path, const std::string &contents);
bool removeFile(const std::string &path);
bool renameFile(const std::string &from, const std::string &to);

// Names of the sub-folders of folder, sorted case-insensitively. Hidden
// entries (starting with '.') are left out.
std::vector<std::string> listFolders(const std::string &folder);

struct Entry {
    std::string name;
    bool folder = false;
    int64_t size = -1; // files only; -1 when it cannot be read
};

// The folders and then the files in folder, each sorted case-insensitively.
// Hidden entries are left out. ok is false when folder cannot be opened.
std::vector<Entry> listEntries(const std::string &folder, bool *ok = nullptr);

} // namespace fs
