#pragma once

#include "file_ops.hpp"
#include "fs_util.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fileindex {

// "sdmc:/a/b/" and "sdmc:/a/b" are the same folder: keys have no trailing
// slash, except a root such as "sdmc:/".
std::string keyOf(const std::string &path);

// What 3dsDL has read of the SD card, so it is not read twice: a folder's
// listing, shown at once when it is opened again (and always read again
// behind it); folder totals, so sorting by size adds up a folder only once;
// and file dates, which the card finds one file at a time.
//
// It is saved between runs, when anything could have changed the card, so
// what is loaded is only a first answer: totals load unverified and are
// added up again before anything relies on them, listings are read again
// as always, and a date counts only while its file keeps the size it had.
// While 3dsDL runs, totals are dropped for every folder above anything that
// changes, and for a folder whose own entries are no longer the ones its
// total was added up from.
//
// Main thread only: a worker gets a copy of the totals and hands back what
// it found.
class Index {
public:
    explicit Index(size_t maxListed = 12000, size_t maxTotals = 20000,
                   size_t maxDated = 20000);

    // The entries folder had when last read whole, read with hidden files
    // or not as given; nullptr when not known.
    const std::vector<fs::Entry> *listing(const std::string &folder, bool hidden);
    void storeListing(const std::string &folder, bool hidden,
                      const std::vector<fs::Entry> &entries);

    // verified, when given, says whether the total was added up while
    // 3dsDL has been running, rather than loaded from the last run.
    bool total(const std::string &folder, fileops::Totals &totals,
               bool *verified = nullptr) const;
    // fingerprint is fs::fingerprint of the folder's own entries as they
    // were when the total was added up.
    void storeTotal(const std::string &folder, const fileops::Totals &totals,
                    uint64_t fingerprint);
    // A copy of the verified totals, by key, for a worker.
    std::map<std::string, fileops::Totals> verifiedTotals() const;

    // When path last changed, if known for a file of size (-1 for a
    // folder).
    bool date(const std::string &path, int64_t size, int64_t &modified);
    void storeDate(const std::string &path, int64_t size, int64_t modified);

    // folder was just read whole: when its entries are not what its total
    // was added up from, its total and those above are dropped. Returns
    // true when it dropped any.
    bool verify(const std::string &folder, const std::vector<fs::Entry> &entries);
    // Something at path was added, removed, renamed or written: its own
    // listing, total and date, everything listed or added up inside it, its
    // folder's listing and the totals of every folder above are dropped.
    void changed(const std::string &path);
    // The same for a folder whose files may have been written over at the
    // same size, so the dates of everything inside it go too.
    void rewritten(const std::string &folder);
    // Anything may have changed, such as after another app ran. Listings
    // and dates stay, as they are checked whenever they are used.
    void forgetTotals();

    // Saved and loaded with card, which says which SD card it describes.
    std::string save(uint64_t card) const;
    // Replaces everything with data; false, leaving it empty, when data is
    // damaged, from another version or about another card.
    bool load(const std::string &data, uint64_t card);
    // Takes from loaded whatever this does not know yet, for a load that
    // finishes after the card has already been read.
    void adopt(Index &&loaded);

    // Changed since it was last saved or loaded.
    bool dirty() const { return dirty_; }
    void markSaved() { dirty_ = false; }

private:
    struct Listing {
        std::vector<fs::Entry> entries;
        bool hidden = false;
        uint64_t used = 0;
    };
    struct Total {
        fileops::Totals totals;
        uint64_t fingerprint = 0;
        bool verified = false;
    };
    struct Dated {
        int64_t size = -1;
        int64_t modified = 0;
    };
    struct DatedFolder {
        std::map<std::string, Dated> entries;
        uint64_t used = 0;
    };

    void dropListing(std::map<std::string, Listing>::iterator at);
    void dropTotalsAbove(std::string key);
    void makeRoomForListing(size_t count);
    void makeRoomForDates();

    size_t maxListed_;
    size_t maxTotals_;
    size_t maxDated_;
    size_t listed_ = 0; // entries in listings_
    size_t dated_ = 0;  // entries in dates_
    uint64_t clock_ = 0;
    bool dirty_ = false;
    std::map<std::string, Listing> listings_;
    std::map<std::string, Total> totals_;
    std::map<std::string, DatedFolder> dates_; // by folder key, then name
};

} // namespace fileindex
