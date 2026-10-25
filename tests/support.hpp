#pragma once

#include <cstdio>
#include <cstdlib>
#include <ftw.h>
#include <string>
#include <unistd.h>

// A scratch folder that is deleted again when the test finishes.
class TempDir {
public:
    TempDir()
    {
        const char *base = std::getenv("TMPDIR");
        std::string pattern = std::string(base != nullptr ? base : "/tmp");
        if (pattern.back() != '/') {
            pattern += '/';
        }
        pattern += "3dsdl-test-XXXXXX";
        if (mkdtemp(&pattern[0]) != nullptr) {
            path_ = pattern + "/";
        }
    }

    ~TempDir()
    {
        if (!path_.empty()) {
            nftw(path_.c_str(), removeEntry, 16, FTW_DEPTH | FTW_PHYS);
        }
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    // Always ends with '/', matching how 3dsDL stores folders.
    const std::string &path() const { return path_; }

private:
    static int removeEntry(const char *path, const struct stat *, int,
                           struct FTW *)
    {
        return std::remove(path);
    }

    std::string path_;
};

inline bool writeText(const std::string &path, const std::string &contents)
{
    FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const bool ok = std::fwrite(contents.data(), 1, contents.size(), file) ==
                    contents.size();
    return std::fclose(file) == 0 && ok;
}

// Whether names in folder ignore case, as FAT's do. Tests of what 3dsDL
// does about that need TMPDIR to ignore case too: macOS's does, and CI
// gives Linux an ext4 folder made with casefold (.github/workflows/ci.yml).
inline bool foldsCase(const std::string &folder)
{
    const std::string probe = folder + "Case-Probe";
    if (!writeText(probe, "")) {
        return false;
    }
    const bool folds = access((folder + "case-probe").c_str(), F_OK) == 0;
    std::remove(probe.c_str());
    return folds;
}

inline std::string readText(const std::string &path)
{
    std::string contents;
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return contents;
    }
    char buffer[4096];
    size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        contents.append(buffer, count);
    }
    std::fclose(file);
    return contents;
}
