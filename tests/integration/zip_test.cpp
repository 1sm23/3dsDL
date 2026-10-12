#include "../check.hpp"
#include "../support.hpp"

#include "fs_util.hpp"
#include "zip.hpp"

#include <cstdlib>

namespace {

std::string fixture(const std::string &name)
{
    const char *folder = std::getenv("FIXTURE_ZIPS");
    return std::string(folder != nullptr ? folder : ".") + "/" + name;
}

std::string binary()
{
    std::string data;
    for (int repeat = 0; repeat < 40; ++repeat) {
        for (int value = 0; value < 256; ++value) {
            data += static_cast<char>(value);
        }
    }
    return data;
}

struct Quiet : zip::Observer {
    int progressCalls = 0;
    int cancelAfterCalls = -1;
    bool cancelRequested() override
    {
        return cancelAfterCalls >= 0 && progressCalls >= cancelAfterCalls;
    }
    void onProgress(uint64_t, uint64_t, const std::string &) override
    {
        ++progressCalls;
    }
};

} // namespace

TEST("zip: reads and extracts stored, deflated and empty entries")
{
    zip::Archive archive;
    std::string error;
    CHECK(zip::readArchive(fixture("basic.zip"), archive, error));
    CHECK_EQ(archive.fileCount, 3u);
    CHECK_EQ(archive.totalSize,
             static_cast<uint64_t>(binary().size() + 17));

    TempDir folder;
    Quiet observer;
    const zip::Result result = zip::extract(fixture("basic.zip"), archive,
                                            folder.path(),
                                            zip::Existing::Skip, observer);
    CHECK(result.ok);
    CHECK_EQ(result.extracted, 3u);
    CHECK_EQ(readText(folder.path() + "readme.txt"),
             std::string("hello from a zip\n"));
    CHECK_EQ(readText(folder.path() + "3ds/App/App.3dsx"), binary());
    CHECK(fs::fileExists(folder.path() + "empty.txt"));
}

TEST("zip: existing files are skipped or overwritten on request")
{
    TempDir folder;
    CHECK(writeText(folder.path() + "readme.txt", "mine"));
    zip::Archive archive;
    std::string error;
    CHECK(zip::readArchive(fixture("basic.zip"), archive, error));

    const std::vector<std::string> existing =
        zip::existingFiles(archive, folder.path());
    CHECK_EQ(existing.size(), static_cast<size_t>(1));

    Quiet observer;
    zip::Result result = zip::extract(fixture("basic.zip"), archive,
                                      folder.path(), zip::Existing::Skip,
                                      observer);
    CHECK(result.ok);
    CHECK_EQ(result.skippedExisting, 1u);
    CHECK_EQ(readText(folder.path() + "readme.txt"), std::string("mine"));

    result = zip::extract(fixture("basic.zip"), archive, folder.path(),
                          zip::Existing::Overwrite, observer);
    CHECK(result.ok);
    CHECK_EQ(readText(folder.path() + "readme.txt"),
             std::string("hello from a zip\n"));
}

TEST("zip: paths that try to escape the folder are refused")
{
    CHECK_EQ(zip::safeRelativePath("../x"), std::string(""));
    CHECK_EQ(zip::safeRelativePath("/x"), std::string(""));
    CHECK_EQ(zip::safeRelativePath("C:/x"), std::string(""));
    CHECK_EQ(zip::safeRelativePath("a/../../x"), std::string(""));
    CHECK_EQ(zip::safeRelativePath("a\\b\\c.txt"), std::string("a/b/c.txt"));
    CHECK_EQ(zip::safeRelativePath("./a//b/"), std::string("a/b"));
    CHECK_EQ(zip::safeRelativePath("a/b?c"), std::string("a/b_c"));

    TempDir outer;
    const std::string inside = outer.path() + "inside/";
    CHECK(fs::makeDirectories(inside));
    zip::Archive archive;
    std::string error;
    CHECK(zip::readArchive(fixture("evil.zip"), archive, error));
    Quiet observer;
    const zip::Result result = zip::extract(fixture("evil.zip"), archive,
                                            inside, zip::Existing::Skip,
                                            observer);
    CHECK(result.ok);
    CHECK_EQ(result.skippedUnsafe, 4u);
    CHECK_EQ(result.extracted, 2u);
    CHECK(fs::fileExists(inside + "good.txt"));
    CHECK(fs::fileExists(inside + "dir/win.txt"));
    CHECK(!fs::fileExists(outer.path() + "escape.txt"));
    CHECK(!fs::fileExists(outer.path() + "up.txt"));
}

TEST("zip: ZIP64 archives and UTF-8 names")
{
    TempDir folder;
    zip::Archive archive;
    std::string error;
    CHECK(zip::readArchive(fixture("zip64.zip"), archive, error));
    Quiet observer;
    zip::Result result = zip::extract(fixture("zip64.zip"), archive,
                                      folder.path(), zip::Existing::Skip,
                                      observer);
    CHECK(result.ok);
    CHECK_EQ(readText(folder.path() + "big.bin"), binary());

    CHECK(zip::readArchive(fixture("utf8.zip"), archive, error));
    result = zip::extract(fixture("utf8.zip"), archive, folder.path(),
                          zip::Existing::Skip, observer);
    CHECK(result.ok);
    CHECK(fs::fileExists(folder.path() + "na\xc3\xafve.txt"));
}

TEST("zip: unsupported compression is skipped, the rest extracted")
{
    TempDir folder;
    zip::Archive archive;
    std::string error;
    CHECK(zip::readArchive(fixture("mixed.zip"), archive, error));
    Quiet observer;
    const zip::Result result = zip::extract(fixture("mixed.zip"), archive,
                                            folder.path(),
                                            zip::Existing::Skip, observer);
    CHECK(result.ok);
    CHECK_EQ(result.skippedUnsupported, 1u);
    CHECK(fs::fileExists(folder.path() + "plain.txt"));
    CHECK(!fs::fileExists(folder.path() + "bz.txt"));
}

TEST("zip: damaged data and truncated archives are detected")
{
    TempDir folder;
    zip::Archive archive;
    std::string error;
    CHECK(zip::readArchive(fixture("corrupt.zip"), archive, error));
    Quiet observer;
    const zip::Result result = zip::extract(fixture("corrupt.zip"), archive,
                                            folder.path(),
                                            zip::Existing::Skip, observer);
    CHECK(!result.ok);
    CHECK(result.message.find("checksum") != std::string::npos);
    CHECK(!fs::fileExists(folder.path() + "data.bin"));
    CHECK(!fs::fileExists(folder.path() + "data.bin.3dsdl-tmp"));

    CHECK(!zip::readArchive(fixture("truncated.zip"), archive, error));
    CHECK(!error.empty());
    CHECK(!zip::readArchive(fixture("missing.zip"), archive, error));
}

TEST("zip: extraction can be cancelled")
{
    TempDir folder;
    zip::Archive archive;
    std::string error;
    CHECK(zip::readArchive(fixture("basic.zip"), archive, error));
    Quiet observer;
    observer.cancelAfterCalls = 1;
    const zip::Result result = zip::extract(fixture("basic.zip"), archive,
                                            folder.path(),
                                            zip::Existing::Skip, observer);
    CHECK(!result.ok);
    CHECK(result.cancelled);
}
