#include "../check.hpp"
#include "../support.hpp"

#include "file_index.hpp"
#include "file_ops.hpp"
#include "fs_util.hpp"

#include <map>
#include <string>
#include <vector>

namespace {

fs::Entry file(const std::string &name, int64_t size)
{
    fs::Entry entry;
    entry.name = name;
    entry.size = size;
    return entry;
}

fs::Entry folder(const std::string &name)
{
    fs::Entry entry;
    entry.name = name;
    entry.folder = true;
    return entry;
}

fileops::Totals bytes(uint64_t count)
{
    fileops::Totals totals;
    totals.files = 1;
    totals.bytes = count;
    return totals;
}

} // namespace

TEST("fileindex: keys ignore a trailing slash but keep the root's")
{
    CHECK_EQ(fileindex::keyOf("sdmc:/a/b/"), std::string("sdmc:/a/b"));
    CHECK_EQ(fileindex::keyOf("sdmc:/a/b"), std::string("sdmc:/a/b"));
    CHECK_EQ(fileindex::keyOf("sdmc:/"), std::string("sdmc:/"));
    CHECK_EQ(fileindex::keyOf("/"), std::string("/"));
}

TEST("fileindex: fingerprints ignore order, times, hidden entries and folder sizes")
{
    std::vector<fs::Entry> a = {file("x.bin", 10), folder("music")};
    std::vector<fs::Entry> b = {folder("music"), file("x.bin", 10)};
    b[1].modified = 12345;
    b[0].size = 900; // a folder's size, once added up
    b.push_back(file(".hidden", 3));
    CHECK_EQ(fs::fingerprint(a), fs::fingerprint(b));
    b[1].size = 11;
    CHECK(fs::fingerprint(a) != fs::fingerprint(b));
    // A file and a folder of the same name differ.
    CHECK(fs::fingerprint({file("x", 0)}) != fs::fingerprint({folder("x")}));
}

TEST("fileindex: a change drops the totals above it and listings around it")
{
    fileindex::Index index;
    index.storeTotal("sdmc:/", bytes(30), 1);
    index.storeTotal("sdmc:/a", bytes(20), 2);
    index.storeTotal("sdmc:/a/b", bytes(10), 3);
    index.storeTotal("sdmc:/a/b/c", bytes(5), 4);
    index.storeTotal("sdmc:/other", bytes(10), 5);
    index.storeListing("sdmc:/a/", false, {folder("b")});
    index.storeListing("sdmc:/a/b/", false, {folder("c")});
    index.storeListing("sdmc:/other/", false, {file("y", 1)});

    index.changed("sdmc:/a/b/");
    fileops::Totals totals;
    CHECK(!index.total("sdmc:/", totals));
    CHECK(!index.total("sdmc:/a/", totals));
    CHECK(!index.total("sdmc:/a/b", totals));
    CHECK(!index.total("sdmc:/a/b/c", totals)); // inside what changed
    CHECK(index.total("sdmc:/other/", totals));
    CHECK_EQ(totals.bytes, static_cast<uint64_t>(10));
    CHECK(index.listing("sdmc:/a", false) == nullptr); // its folder
    CHECK(index.listing("sdmc:/a/b", false) == nullptr);
    CHECK(index.listing("sdmc:/other", false) != nullptr);
    // Read with hidden files shown, the listing does not count.
    CHECK(index.listing("sdmc:/other", true) == nullptr);
}

TEST("fileindex: a folder read again with other entries drops its totals")
{
    fileindex::Index index;
    const std::vector<fs::Entry> entries = {file("song.mp3", 400), folder("art")};
    index.storeTotal("sdmc:/", bytes(900), 7);
    index.storeTotal("sdmc:/music", bytes(500), fs::fingerprint(entries));
    index.storeTotal("sdmc:/music/art", bytes(100), 8);

    CHECK(!index.verify("sdmc:/music/", entries));
    std::vector<fs::Entry> renamed = entries;
    renamed[0].name = "track.mp3";
    CHECK(index.verify("sdmc:/music/", renamed));
    fileops::Totals totals;
    CHECK(!index.total("sdmc:/music", totals));
    CHECK(!index.total("sdmc:/", totals));
    CHECK(index.total("sdmc:/music/art", totals)); // its own entries are fine
}

TEST("fileindex: listings make room by dropping the longest unused")
{
    fileindex::Index index(4);
    index.storeListing("sdmc:/a", false, {file("1", 1), file("2", 1)});
    index.storeListing("sdmc:/b", false, {file("1", 1), file("2", 1)});
    CHECK(index.listing("sdmc:/a", false) != nullptr); // a is used again
    index.storeListing("sdmc:/c", false, {file("1", 1)});
    CHECK(index.listing("sdmc:/a", false) != nullptr);
    CHECK(index.listing("sdmc:/b", false) == nullptr);
    CHECK(index.listing("sdmc:/c", false) != nullptr);
    // Too big to keep at all.
    index.storeListing("sdmc:/d", false,
                       {file("1", 1), file("2", 1), file("3", 1), file("4", 1),
                        file("5", 1)});
    CHECK(index.listing("sdmc:/d", false) == nullptr);
}

TEST("fileindex: measure reuses known totals and reports what it adds up")
{
    TempDir temp;
    const std::string root = temp.path() + "top";
    CHECK(fs::makeDirectories(root + "/inner/deep/"));
    CHECK(writeText(root + "/a.txt", "hello"));
    CHECK(writeText(root + "/inner/b.txt", "123"));
    CHECK(writeText(root + "/inner/deep/c.txt", "1234567"));

    fileindex::Index index;
    fileops::MeasureHooks hooks;
    hooks.measured = [&](const std::string &path, const fileops::Totals &totals,
                         uint64_t print) { index.storeTotal(path, totals, print); };
    const fileops::Totals first = fileops::measure(root, nullptr, nullptr, &hooks);
    CHECK_EQ(first.bytes, static_cast<uint64_t>(15));
    fileops::Totals inner;
    CHECK(index.total(root + "/inner/", inner));
    CHECK_EQ(inner.bytes, static_cast<uint64_t>(10));
    CHECK_EQ(inner.files, static_cast<uint64_t>(2));
    CHECK_EQ(inner.folders, static_cast<uint64_t>(1));
    // Its own fingerprint, from its own entries.
    CHECK(!index.verify(root + "/inner", fs::listEntries(root + "/inner", nullptr, true)));

    // A known total is used without reading the folder, even when the card
    // now says otherwise.
    CHECK(writeText(root + "/inner/deep/d.txt", "more"));
    const std::map<std::string, fileops::Totals> known = index.verifiedTotals();
    hooks.known = [&](const std::string &path, fileops::Totals &totals) {
        const auto found = known.find(fileindex::keyOf(path));
        if (found == known.end()) {
            return false;
        }
        totals = found->second;
        return true;
    };
    hooks.measured = nullptr;
    CHECK_EQ(fileops::measure(root + "/inner", nullptr, nullptr, &hooks).bytes,
             static_cast<uint64_t>(10));
    // Once told, it reads it again.
    index.changed(root + "/inner/deep/d.txt");
    CHECK(!index.total(root + "/inner", inner));
    CHECK(!index.total(root, inner));
}

TEST("fileindex: dates count only while the file keeps its size")
{
    fileindex::Index index;
    index.storeDate("sdmc:/music/song.mp3", 400, 1700000000);
    index.storeDate("sdmc:/music/art", -1, 1600000000); // a folder
    int64_t modified = 0;
    CHECK(index.date("sdmc:/music/song.mp3", 400, modified));
    CHECK_EQ(modified, static_cast<int64_t>(1700000000));
    CHECK(!index.date("sdmc:/music/song.mp3", 401, modified));
    CHECK(index.date("sdmc:/music/art/", -1, modified));
    index.storeDate("sdmc:/music/none.mp3", 1, 0); // unknown is not kept
    CHECK(!index.date("sdmc:/music/none.mp3", 1, modified));
    // A change drops its own date, not its neighbours'.
    index.changed("sdmc:/music/art");
    CHECK(!index.date("sdmc:/music/art", -1, modified));
    CHECK(index.date("sdmc:/music/song.mp3", 400, modified));

    // Past the limit, the folder unused longest goes.
    fileindex::Index small(100, 100, 2);
    small.storeDate("sdmc:/a/1", 1, 5);
    small.storeDate("sdmc:/b/1", 1, 5);
    small.storeDate("sdmc:/c/1", 1, 5);
    CHECK(!small.date("sdmc:/a/1", 1, modified));
    CHECK(small.date("sdmc:/c/1", 1, modified));
}

TEST("fileindex: saved and loaded, totals come back unverified")
{
    fileindex::Index index;
    fileops::Totals totals = bytes(500);
    totals.folders = 2;
    std::vector<fs::Entry> entries = {file("song.mp3", 400), folder("art")};
    index.storeTotal("sdmc:/music", totals, fs::fingerprint(entries));
    entries[1].size = 999; // a total, which a listing does not keep
    index.storeListing("sdmc:/music/", true, entries);
    index.storeDate("sdmc:/music/song.mp3", 400, 1700000000);
    CHECK(index.dirty());
    const std::string saved = index.save(0x1234);

    fileindex::Index loaded;
    CHECK(loaded.load(saved, 0x1234));
    CHECK(!loaded.dirty());
    fileops::Totals back;
    bool verified = true;
    CHECK(loaded.total("sdmc:/music", back, &verified));
    CHECK(!verified);
    CHECK_EQ(back.bytes, static_cast<uint64_t>(500));
    CHECK_EQ(back.folders, static_cast<uint64_t>(2));
    CHECK(loaded.verifiedTotals().empty()); // workers add it up again
    const std::vector<fs::Entry> *listing = loaded.listing("sdmc:/music", true);
    CHECK(listing != nullptr && listing->size() == 2);
    CHECK_EQ((*listing)[1].size, static_cast<int64_t>(-1));
    int64_t modified = 0;
    CHECK(loaded.date("sdmc:/music/song.mp3", 400, modified));
    // Its fingerprint came along: the same entries still match.
    CHECK(!loaded.verify("sdmc:/music", entries));
    // Added up again, it counts.
    loaded.storeTotal("sdmc:/music", totals, fs::fingerprint(entries));
    CHECK(loaded.total("sdmc:/music", back, &verified) && verified);
}

TEST("fileindex: a damaged or foreign index is not loaded")
{
    fileindex::Index index;
    index.storeTotal("sdmc:/music", bytes(500), 1);
    const std::string saved = index.save(9);
    fileindex::Index loaded;
    CHECK(!loaded.load(saved, 10)); // another card
    std::string damaged = saved;
    damaged[damaged.size() / 2] ^= 0x40;
    CHECK(!loaded.load(damaged, 9));
    CHECK(!loaded.load(saved.substr(0, saved.size() - 3), 9));
    CHECK(!loaded.load("", 9));
    fileops::Totals totals;
    CHECK(!loaded.total("sdmc:/music", totals)); // left empty
    CHECK(loaded.load(saved, 9));
}

TEST("fileindex: a late load fills in only what is not known")
{
    fileindex::Index index;
    index.storeTotal("sdmc:/fresh", bytes(10), 1);
    fileindex::Index old;
    old.storeTotal("sdmc:/fresh", bytes(99), 1);
    old.storeTotal("sdmc:/other", bytes(5), 2);
    old.storeDate("sdmc:/other/a", 1, 42);
    fileindex::Index loaded;
    CHECK(loaded.load(old.save(0), 0));
    index.adopt(std::move(loaded));
    fileops::Totals totals;
    bool verified = false;
    CHECK(index.total("sdmc:/fresh", totals, &verified));
    CHECK_EQ(totals.bytes, static_cast<uint64_t>(10));
    CHECK(verified);
    CHECK(index.total("sdmc:/other", totals, &verified));
    CHECK(!verified);
    int64_t modified = 0;
    CHECK(index.date("sdmc:/other/a", 1, modified));
}

TEST("fs: unknown dates sort last either way")
{
    std::vector<fs::Entry> entries = {file("old", 1), file("unknown", 1),
                                      file("new", 1)};
    entries[0].modified = 100;
    entries[2].modified = 200;
    fs::sortEntries(entries, fs::SortOrder::Date);
    CHECK_EQ(entries[0].name, std::string("new"));
    CHECK_EQ(entries[2].name, std::string("unknown"));
    fs::sortEntries(entries, fs::SortOrder::Date, true);
    CHECK_EQ(entries[0].name, std::string("old"));
    CHECK_EQ(entries[2].name, std::string("unknown"));
}
