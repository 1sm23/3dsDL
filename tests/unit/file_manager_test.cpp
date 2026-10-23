#include "../check.hpp"
#include "../support.hpp"

#include "file_ops.hpp"
#include "file_types.hpp"
#include "fs_util.hpp"
#include "image.hpp"
#include "picture.hpp"
#include "records.hpp"
#include "sha256.hpp"
#include "smdh.hpp"
#include "text_doc.hpp"
#include "title_meta.hpp"

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace {

class Quiet : public fileops::Observer {
public:
    bool cancelRequested() override { return cancelAfter >= 0 && calls++ >= cancelAfter; }
    void onProgress(uint64_t done, uint64_t total,
                    const std::string &) override
    {
        lastDone = done;
        lastTotal = total;
    }
    int cancelAfter = -1;
    int calls = 0;
    uint64_t lastDone = 0;
    uint64_t lastTotal = 0;
};

void putLe32(std::string &data, size_t offset, uint32_t value)
{
    for (int index = 0; index < 4; ++index) {
        data[offset + index] = static_cast<char>(value >> (8 * index));
    }
}

void putUtf16(std::string &data, size_t offset, const std::string &ascii)
{
    for (size_t index = 0; index < ascii.size(); ++index) {
        data[offset + index * 2] = ascii[index];
        data[offset + index * 2 + 1] = 0;
    }
}

std::string makeSmdh(const std::string &name, const std::string &publisher)
{
    std::string block(smdh::Size, '\0');
    std::memcpy(&block[0], "SMDH", 4);
    const size_t english = 0x8 + 0x200;
    putUtf16(block, english, name);
    putUtf16(block, english + 0x180, publisher);
    block[0x24c0] = 0x1f; // one blue pixel at (0, 0)
    return block;
}

} // namespace

TEST("filetype: classifies by extension")
{
    using filetype::Kind;
    CHECK(filetype::fromName("Game.CIA") == Kind::Cia);
    CHECK(filetype::fromName("app.3dsx") == Kind::Homebrew);
    CHECK(filetype::fromName("rom.3ds") == Kind::CartridgeImage);
    CHECK(filetype::fromName("rom.nds") == Kind::Nds);
    CHECK(filetype::fromName("photo.JPEG") == Kind::Image);
    CHECK(filetype::fromName("config.ini") == Kind::Text);
    CHECK(filetype::fromName("README.md") == Kind::Text);
    CHECK(filetype::fromName("boot.firm") == Kind::Firm);
    CHECK(filetype::fromName("song.mp3") == Kind::Audio);
    CHECK(filetype::fromName("pack.zip") == Kind::Zip);
    CHECK(filetype::fromName("noextension") == Kind::Other);
    CHECK(filetype::fromName(".hidden") == Kind::Other);
    CHECK(filetype::hasTitleMeta(Kind::Nds));
    CHECK(!filetype::hasTitleMeta(Kind::Image));
}

TEST("fileops: copy, move and delete files and folders")
{
    TempDir temp;
    const std::string root = temp.path();
    CHECK(fs::makeDirectories(root + "src/inner/"));
    CHECK(writeText(root + "src/a.txt", "hello"));
    CHECK(writeText(root + "src/inner/b.bin", std::string(300000, 'x')));

    const fileops::Totals totals = fileops::measure(root + "src");
    CHECK_EQ(totals.files, static_cast<uint64_t>(2));
    CHECK_EQ(totals.folders, static_cast<uint64_t>(1));
    CHECK_EQ(totals.bytes, static_cast<uint64_t>(300005));

    // Stopped before it starts, the count is empty.
    const std::atomic<bool> stopped(true);
    CHECK_EQ(fileops::measure(root + "src", &stopped).files,
             static_cast<uint64_t>(0));
    // The checksum counts what it reads, and gives up when stopped.
    std::atomic<uint64_t> read(0);
    CHECK(!sha256::ofFile(root + "src/inner/b.bin", nullptr, &read).empty());
    CHECK_EQ(read.load(), static_cast<uint64_t>(300000));
    CHECK_EQ(sha256::ofFile(root + "src/inner/b.bin", &stopped), std::string(""));

    Quiet observer;
    uint64_t done = 0;
    fileops::Result result = fileops::copy(root + "src", root + "copy",
                                           observer, done, totals.bytes);
    CHECK(result.status == fileops::Status::Ok);
    CHECK_EQ(result.files, static_cast<uint64_t>(2));
    CHECK_EQ(done, totals.bytes);
    CHECK_EQ(readText(root + "copy/inner/b.bin").size(),
             static_cast<size_t>(300000));
    CHECK_EQ(readText(root + "copy/a.txt"), std::string("hello"));

    // Never over an existing entry, never into itself.
    result = fileops::copy(root + "src", root + "copy", observer, done, 0);
    CHECK(result.status == fileops::Status::Failed);
    result = fileops::copy(root + "src", root + "src/inner/again", observer,
                           done, 0);
    CHECK(result.status == fileops::Status::Failed);
    CHECK(!fileops::exists(root + "src/inner/again"));

    std::string error;
    CHECK(fileops::move(root + "copy/a.txt", root + "moved.txt", error));
    CHECK(!fileops::exists(root + "copy/a.txt"));
    CHECK(!fileops::move(root + "src/a.txt", root + "moved.txt", error));
    CHECK(!fileops::move(root + "src", root + "src/inner/x", error));

    uint64_t deleted = 0;
    result = fileops::remove(root + "copy", observer, deleted, 1);
    CHECK(result.status == fileops::Status::Ok);
    CHECK_EQ(deleted, static_cast<uint64_t>(1));
    CHECK(!fileops::exists(root + "copy"));
}

TEST("fileops: a cancelled copy removes the unfinished file")
{
    TempDir temp;
    CHECK(writeText(temp.path() + "big.bin", std::string(600000, 'y')));
    Quiet observer;
    observer.cancelAfter = 1;
    uint64_t done = 0;
    const fileops::Result result = fileops::copy(
        temp.path() + "big.bin", temp.path() + "copy.bin", observer, done,
        600000);
    CHECK(result.status == fileops::Status::Cancelled);
    CHECK(!fileops::exists(temp.path() + "copy.bin"));
}

TEST("fileops: names and paths")
{
    std::string error;
    CHECK(fileops::validName("My Folder", error));
    CHECK(fileops::validName("config.ini", error));
    CHECK(!fileops::validName("", error));
    CHECK(!fileops::validName("..", error));
    CHECK(!fileops::validName("a/b", error));
    CHECK(!fileops::validName("what?", error));
    CHECK(!fileops::validName("trailing.", error));

    CHECK(fileops::isInside("sdmc:/a/b", "sdmc:/a"));
    CHECK(fileops::isInside("sdmc:/a", "sdmc:/a/"));
    CHECK(!fileops::isInside("sdmc:/ab", "sdmc:/a"));
    CHECK_EQ(fileops::name("sdmc:/3ds/app/"), std::string("app"));
    CHECK_EQ(fileops::name("sdmc:/x.txt"), std::string("x.txt"));
}

TEST("fs: hidden entries and sort orders")
{
    TempDir temp;
    CHECK(writeText(temp.path() + "b.txt", "12"));
    CHECK(writeText(temp.path() + "a.zip", "1234"));
    CHECK(writeText(temp.path() + ".secret", "1"));
    CHECK(fs::makeDirectories(temp.path() + "folder/"));

    std::vector<fs::Entry> entries = fs::listEntries(temp.path());
    CHECK_EQ(entries.size(), static_cast<size_t>(3));
    entries = fs::listEntries(temp.path(), nullptr, true);
    CHECK_EQ(entries.size(), static_cast<size_t>(4));

    fs::sortEntries(entries, fs::SortOrder::Size);
    CHECK_EQ(entries[0].name, std::string("folder"));
    CHECK_EQ(entries[1].name, std::string("a.zip"));
    CHECK_EQ(entries[3].name, std::string(".secret"));

    fs::sortEntries(entries, fs::SortOrder::Type);
    CHECK_EQ(entries[1].name, std::string(".secret")); // no extension first
    CHECK_EQ(entries[2].name, std::string("b.txt"));
    CHECK_EQ(entries[3].name, std::string("a.zip"));

    Settings settings;
    settings.showHiddenFiles = true;
    settings.fileSort = fs::SortOrder::Date;
    const Settings parsed = parseSettings(serializeSettings(settings));
    CHECK(parsed.showHiddenFiles);
    CHECK(parsed.fileSort == fs::SortOrder::Date);
}

TEST("textdoc: keeps line endings, BOM and the final newline")
{
    textdoc::Document unix = textdoc::parse("a\nb\n");
    CHECK_EQ(unix.lines.size(), static_cast<size_t>(2));
    CHECK(!unix.crlf);
    CHECK(unix.finalNewline);
    unix.lines[1] = "changed";
    CHECK_EQ(textdoc::serialize(unix), std::string("a\nchanged\n"));

    const std::string windows = "\xef\xbb\xbf[core]\r\nkey=1\r\nlast";
    textdoc::Document document = textdoc::parse(windows);
    CHECK(document.bom);
    CHECK(document.crlf);
    CHECK(!document.finalNewline);
    CHECK_EQ(document.lines[0], std::string("[core]"));
    CHECK_EQ(textdoc::serialize(document), windows);

    const textdoc::Document empty = textdoc::parse("");
    CHECK_EQ(empty.lines.size(), static_cast<size_t>(1));
    CHECK_EQ(textdoc::serialize(empty), std::string(""));

    CHECK(textdoc::looksLikeText("plain text\twith tab\n"));
    CHECK(!textdoc::looksLikeText(std::string("bin\0ary", 7)));
}

TEST("textdoc: load, save and read-only cases")
{
    TempDir temp;
    const std::string path = temp.path() + "config.ini";
    CHECK(writeText(path, "x=1\r\ny=2\r\n"));
    textdoc::Document document;
    CHECK(textdoc::load(path, document) == textdoc::Load::Ok);
    CHECK(!document.readOnly);
    document.lines.insert(document.lines.begin() + 1, "z=3");
    CHECK(textdoc::save(path, document));
    CHECK_EQ(readText(path), std::string("x=1\r\nz=3\r\ny=2\r\n"));

    CHECK(writeText(temp.path() + "big.txt", std::string(100, 'a') + "\n" +
                                                 std::string(100, 'b')));
    CHECK(textdoc::load(temp.path() + "big.txt", document, 150) ==
          textdoc::Load::Ok);
    CHECK(document.readOnly);
    CHECK(document.truncated);
    CHECK_EQ(document.lines.size(), static_cast<size_t>(1));

    CHECK(writeText(temp.path() + "latin.txt", "caf\xe9\n"));
    CHECK(textdoc::load(temp.path() + "latin.txt", document) ==
          textdoc::Load::Ok);
    CHECK(document.readOnly);
    CHECK_EQ(document.lines[0], std::string("caf\xc3\xa9"));

    CHECK(writeText(temp.path() + "bin", std::string("\0\1\2", 3)));
    CHECK(textdoc::load(temp.path() + "bin", document) ==
          textdoc::Load::NotText);
    CHECK(textdoc::head(temp.path() + "bin", 3).empty());
    CHECK_EQ(textdoc::head(path, 2).size(), static_cast<size_t>(2));
}

TEST("titlemeta: 3dsx, smdh, game images, DS games and tickets")
{
    TempDir temp;
    titlemeta::Meta meta;
    std::string error;

    // A .3dsx with an extended header pointing at its SMDH.
    std::string homebrew(0x40, '\0');
    std::memcpy(&homebrew[0], "3DSX", 4);
    homebrew[4] = 0x2c;
    putLe32(homebrew, 0x20, 0x40);
    putLe32(homebrew, 0x24, smdh::Size);
    homebrew += makeSmdh("Homebrew", "Someone");
    CHECK(writeText(temp.path() + "app.3dsx", homebrew));
    CHECK(titlemeta::read(temp.path() + "app.3dsx",
                          filetype::Kind::Homebrew, meta, error));
    CHECK_EQ(meta.name, std::string("Homebrew"));
    CHECK_EQ(meta.publisher, std::string("Someone"));
    CHECK_EQ(meta.icon.size(), static_cast<size_t>(48 * 48));

    // A decrypted .3ds whose ExeFS holds an icon.
    std::string cart(0x4000 + smdh::Size, '\0');
    std::memcpy(&cart[0x100], "NCSD", 4);
    putLe32(cart, 0x108, 0x00164800);
    putLe32(cart, 0x10c, 0x00040000);
    putLe32(cart, 0x120, 0x10); // partition 0 at 0x2000
    std::memcpy(&cart[0x2100], "NCCH", 4);
    std::memcpy(&cart[0x2150], "CTR-P-TEST", 10);
    cart[0x218f] = 0x04;        // NoCrypto
    putLe32(cart, 0x21a0, 0x8); // ExeFS at 0x2000 + 0x1000
    std::memcpy(&cart[0x3000], "icon", 4);
    putLe32(cart, 0x3008, 0x800);
    const std::string icon = makeSmdh("Cartridge", "Maker");
    cart.replace(0x3000 + 0x200 + 0x800, icon.size(), icon);
    CHECK(writeText(temp.path() + "game.3ds", cart));
    CHECK(titlemeta::read(temp.path() + "game.3ds",
                          filetype::Kind::CartridgeImage, meta, error));
    CHECK(meta.hasTitleId);
    CHECK_EQ(meta.titleId, static_cast<uint64_t>(0x0004000000164800ULL));
    CHECK_EQ(meta.productCode, std::string("CTR-P-TEST"));
    CHECK_EQ(meta.name, std::string("Cartridge"));

    cart[0x218f] = 0;
    CHECK(writeText(temp.path() + "locked.3ds", cart));
    CHECK(titlemeta::read(temp.path() + "locked.3ds",
                          filetype::Kind::CartridgeImage, meta, error));
    CHECK(meta.name.empty());
    CHECK(!meta.note.empty());

    // A DS game with a banner whose title has a publisher line.
    std::string nds(0x200 + 0x840, '\0');
    std::memcpy(&nds[0], "TESTGAME", 8);
    std::memcpy(&nds[0x0c], "ABCE", 4);
    putLe32(nds, 0x68, 0x200);
    putLe32(nds, 0x200 + 0x220 + 2, 0x7fff); // colour 1 is white
    nds[0x200 + 0x20] = 0x01;                // first pixel uses colour 1
    putUtf16(nds, 0x200 + 0x340, "Test Game\nPublisher");
    CHECK(writeText(temp.path() + "game.nds", nds));
    CHECK(titlemeta::read(temp.path() + "game.nds", filetype::Kind::Nds, meta,
                          error));
    CHECK_EQ(meta.name, std::string("Test Game"));
    CHECK_EQ(meta.publisher, std::string("Publisher"));
    CHECK_EQ(meta.productCode, std::string("ABCE"));
    CHECK_EQ(meta.icon.size(), static_cast<size_t>(48 * 48));
    if (meta.icon.size() == 48 * 48) {
        CHECK_EQ(meta.icon[0], static_cast<uint16_t>(0xffff));
    }

    // A ticket signed with RSA-2048.
    std::string ticket(0x350, '\0');
    ticket[1] = 0x01;
    ticket[3] = 0x04;
    const unsigned char id[8] = {0x00, 0x04, 0x00, 0x00,
                                 0x00, 0x16, 0x48, 0x00};
    std::memcpy(&ticket[0x140 + 0x9c], id, 8);
    CHECK(writeText(temp.path() + "a.tik", ticket));
    CHECK(titlemeta::read(temp.path() + "a.tik", filetype::Kind::Ticket, meta,
                          error));
    CHECK_EQ(meta.titleId, static_cast<uint64_t>(0x0004000000164800ULL));

    CHECK(!titlemeta::read(temp.path() + "a.tik", filetype::Kind::Homebrew,
                           meta, error));
}

TEST("picture: fits, enlarges and blends transparency")
{
    // 4x2 opaque red shrunk into 2x2 keeps the shape: 2x1.
    std::vector<uint8_t> red(4 * 2 * 4, 0);
    for (size_t index = 0; index < red.size(); index += 4) {
        red[index] = 255;
        red[index + 3] = 255;
    }
    picture::Picture result;
    picture::fit(red.data(), 4, 4, 2, 2, 2, 0, result);
    CHECK_EQ(result.width, static_cast<uint32_t>(2));
    CHECK_EQ(result.height, static_cast<uint32_t>(1));
    CHECK_EQ(result.pixels[0], static_cast<uint16_t>(0xf800));

    // A 2x2 picture in a 9x11 box is enlarged by the whole factor 4.
    const uint8_t rgb[12] = {0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255};
    picture::fit(rgb, 3, 2, 2, 9, 11, 0, result);
    CHECK_EQ(result.width, static_cast<uint32_t>(8));
    CHECK_EQ(result.pixels.size(), static_cast<size_t>(64));
    CHECK_EQ(result.pixels[63], static_cast<uint16_t>(0x001f));

    // Fully transparent pixels show the background.
    const uint8_t clear[4] = {255, 255, 255, 0};
    picture::fit(clear, 4, 1, 1, 1, 1, 0x07e0, result);
    CHECK_EQ(result.pixels[0], static_cast<uint16_t>(0x07e0));
}

TEST("picture: decodes a BMP file and refuses non-pictures")
{
    TempDir temp;
    // A 2x1 24-bit BMP: blue then green (rows are BGR, padded to 4 bytes).
    std::string bmp(54 + 8, '\0');
    bmp[0] = 'B';
    bmp[1] = 'M';
    putLe32(bmp, 2, static_cast<uint32_t>(bmp.size()));
    putLe32(bmp, 10, 54);
    putLe32(bmp, 14, 40);
    putLe32(bmp, 18, 2);
    putLe32(bmp, 22, 1);
    bmp[26] = 1;
    bmp[28] = 24;
    putLe32(bmp, 34, 8);
    bmp[54] = static_cast<char>(255); // blue
    bmp[58] = static_cast<char>(255); // green
    CHECK(writeText(temp.path() + "two.bmp", bmp));

    uint32_t width = 0;
    uint32_t height = 0;
    CHECK(picture::size(temp.path() + "two.bmp", width, height));
    CHECK_EQ(width, static_cast<uint32_t>(2));

    picture::Picture result;
    std::string error;
    CHECK(picture::load(temp.path() + "two.bmp", 400, 240, 0, result, error));
    CHECK_EQ(result.sourceWidth, static_cast<uint32_t>(2));
    CHECK_EQ(result.width, static_cast<uint32_t>(400));
    CHECK_EQ(result.height, static_cast<uint32_t>(200));
    CHECK_EQ(result.pixels.front(), static_cast<uint16_t>(0x001f));
    CHECK_EQ(result.pixels.back(), static_cast<uint16_t>(0x07e0));

    CHECK(writeText(temp.path() + "fake.png", "not a picture"));
    CHECK(!picture::load(temp.path() + "fake.png", 400, 240, 0, result,
                         error));
    CHECK(!error.empty());
}
