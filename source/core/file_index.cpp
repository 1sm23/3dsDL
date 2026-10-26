#include "file_index.hpp"

#include <algorithm>
#include <cstring>

namespace fileindex {

namespace {

// The folder key holds, or "" for a root.
std::string parentKey(const std::string &key)
{
    if (key.empty() || key.back() == '/') {
        return ""; // a root
    }
    const size_t slash = key.find_last_of('/');
    if (slash == std::string::npos) {
        return "";
    }
    // The root keeps its slash: "sdmc:/a" -> "sdmc:/", "/tmp" -> "/".
    if (slash == 0 || key[slash - 1] == ':') {
        return key.substr(0, slash + 1);
    }
    return key.substr(0, slash);
}

std::string nameOf(const std::string &key)
{
    return key.substr(key.find_last_of('/') + 1);
}

// Whether key is inside folder (a key), at any depth.
bool inside(const std::string &key, const std::string &folder)
{
    const std::string prefix = folder.back() == '/' ? folder : folder + "/";
    return key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0;
}

template <typename Map>
void eraseInside(Map &map, const std::string &folder)
{
    const std::string prefix = folder.back() == '/' ? folder : folder + "/";
    for (auto at = map.lower_bound(prefix);
         at != map.end() && at->first.compare(0, prefix.size(), prefix) == 0;) {
        at = map.erase(at);
    }
}

// --- The saved form ----------------------------------------------------------
//
// "3dsDLidx", a version, the card, then the totals, listings and dates, and
// a checksum of all of it, little-endian throughout. Strings carry a 16-bit
// length.

constexpr char Magic[8] = {'3', 'd', 's', 'D', 'L', 'i', 'd', 'x'};
constexpr uint32_t Version = 1;

uint64_t checksum(const char *data, size_t size)
{
    uint64_t hash = 14695981039346656037ULL; // FNV-1a
    for (size_t index = 0; index < size; ++index) {
        hash = (hash ^ static_cast<unsigned char>(data[index])) * 1099511628211ULL;
    }
    return hash;
}

class Writer {
public:
    void bytes(const void *data, size_t size)
    {
        out.append(static_cast<const char *>(data), size);
    }
    void number(uint64_t value, int width)
    {
        for (int index = 0; index < width; ++index) {
            out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
        }
    }
    void text(const std::string &value)
    {
        const size_t size = std::min<size_t>(value.size(), 0xFFFF);
        number(size, 2);
        out.append(value, 0, size);
    }
    std::string out;
};

class Reader {
public:
    Reader(const char *data, size_t size) : data_(data), size_(size) {}
    bool bytes(void *into, size_t size)
    {
        if (size_ - at_ < size) {
            return false;
        }
        std::memcpy(into, data_ + at_, size);
        at_ += size;
        return true;
    }
    bool number(uint64_t &value, int width)
    {
        if (size_ - at_ < static_cast<size_t>(width)) {
            return false;
        }
        value = 0;
        for (int index = 0; index < width; ++index) {
            value |= static_cast<uint64_t>(static_cast<unsigned char>(data_[at_ + index]))
                     << (8 * index);
        }
        at_ += width;
        return true;
    }
    bool signedNumber(int64_t &value)
    {
        uint64_t raw = 0;
        if (!number(raw, 8)) {
            return false;
        }
        value = static_cast<int64_t>(raw);
        return true;
    }
    bool text(std::string &value)
    {
        uint64_t size = 0;
        if (!number(size, 2) || size_ - at_ < size) {
            return false;
        }
        value.assign(data_ + at_, size);
        at_ += size;
        return true;
    }
    // A count of items at least minimum bytes each, so a damaged count
    // cannot ask for more than the data could hold.
    bool count(uint64_t &value, size_t minimum)
    {
        return number(value, 4) && value <= (size_ - at_) / minimum;
    }
    bool atEnd() const { return at_ == size_; }

private:
    const char *data_;
    size_t size_;
    size_t at_ = 0;
};

} // namespace

std::string keyOf(const std::string &path)
{
    std::string key = path;
    while (key.size() > 1 && key.back() == '/' && key[key.size() - 2] != ':') {
        key.pop_back();
    }
    return key;
}

Index::Index(size_t maxListed, size_t maxTotals, size_t maxDated)
    : maxListed_(maxListed), maxTotals_(maxTotals), maxDated_(maxDated)
{
}

const std::vector<fs::Entry> *Index::listing(const std::string &folder, bool hidden)
{
    const auto found = listings_.find(keyOf(folder));
    if (found == listings_.end() || found->second.hidden != hidden) {
        return nullptr;
    }
    found->second.used = ++clock_;
    return &found->second.entries;
}

void Index::storeListing(const std::string &folder, bool hidden,
                         const std::vector<fs::Entry> &entries)
{
    const std::string key = keyOf(folder);
    const auto old = listings_.find(key);
    if (old != listings_.end()) {
        dropListing(old);
    }
    dirty_ = true;
    if (entries.size() > maxListed_) {
        return;
    }
    makeRoomForListing(entries.size());
    Listing &listing = listings_[key];
    listing.entries = entries;
    // A folder's size is its total, kept with the totals.
    for (fs::Entry &entry : listing.entries) {
        if (entry.folder) {
            entry.size = -1;
        }
    }
    listing.hidden = hidden;
    listing.used = ++clock_;
    listed_ += entries.size();
}

bool Index::total(const std::string &folder, fileops::Totals &totals,
                  bool *verified) const
{
    const auto found = totals_.find(keyOf(folder));
    if (found == totals_.end()) {
        return false;
    }
    totals = found->second.totals;
    if (verified != nullptr) {
        *verified = found->second.verified;
    }
    return true;
}

void Index::storeTotal(const std::string &folder, const fileops::Totals &totals,
                       uint64_t fingerprint)
{
    if (totals_.size() >= maxTotals_) {
        totals_.clear(); // starts over rather than track what is oldest
    }
    totals_[keyOf(folder)] = {totals, fingerprint, true};
    dirty_ = true;
}

std::map<std::string, fileops::Totals> Index::verifiedTotals() const
{
    std::map<std::string, fileops::Totals> copy;
    for (const auto &total : totals_) {
        if (total.second.verified) {
            copy.emplace(total.first, total.second.totals);
        }
    }
    return copy;
}

bool Index::date(const std::string &path, int64_t size, int64_t &modified)
{
    const std::string key = keyOf(path);
    const auto folder = dates_.find(parentKey(key));
    if (folder == dates_.end()) {
        return false;
    }
    const auto found = folder->second.entries.find(nameOf(key));
    if (found == folder->second.entries.end() || found->second.size != size) {
        return false;
    }
    folder->second.used = ++clock_;
    modified = found->second.modified;
    return true;
}

void Index::storeDate(const std::string &path, int64_t size, int64_t modified)
{
    const std::string key = keyOf(path);
    const std::string parent = parentKey(key);
    if (modified <= 0 || parent.empty()) {
        return;
    }
    DatedFolder &folder = dates_[parent];
    folder.used = ++clock_;
    const auto inserted = folder.entries.insert({nameOf(key), {size, modified}});
    if (inserted.second) {
        ++dated_;
    } else {
        inserted.first->second = {size, modified};
    }
    dirty_ = true;
    makeRoomForDates();
}

bool Index::verify(const std::string &folder, const std::vector<fs::Entry> &entries)
{
    const std::string key = keyOf(folder);
    const auto found = totals_.find(key);
    if (found == totals_.end() || found->second.fingerprint == fs::fingerprint(entries)) {
        return false;
    }
    // What is inside may be fine, but this folder and those above are not.
    dropTotalsAbove(key);
    dirty_ = true;
    return true;
}

void Index::changed(const std::string &path)
{
    const std::string key = keyOf(path);
    const auto listing = listings_.find(key);
    if (listing != listings_.end()) {
        dropListing(listing);
    }
    for (auto at = listings_.begin(); at != listings_.end();) {
        if (inside(at->first, key)) {
            listed_ -= at->second.entries.size();
            at = listings_.erase(at);
        } else {
            ++at;
        }
    }
    const auto parent = listings_.find(parentKey(key));
    if (parent != listings_.end()) {
        dropListing(parent);
    }
    // Its own date; dates inside it are checked against sizes as they are.
    const auto dated = dates_.find(parentKey(key));
    if (dated != dates_.end() && dated->second.entries.erase(nameOf(key)) > 0) {
        --dated_;
    }
    eraseInside(totals_, key);
    dropTotalsAbove(key);
    dirty_ = true;
}

void Index::rewritten(const std::string &folder)
{
    changed(folder);
    const std::string key = keyOf(folder);
    const auto own = dates_.find(key);
    if (own != dates_.end()) {
        dated_ -= own->second.entries.size();
        dates_.erase(own);
    }
    const std::string prefix = key.back() == '/' ? key : key + "/";
    for (auto at = dates_.lower_bound(prefix);
         at != dates_.end() &&
         at->first.compare(0, prefix.size(), prefix) == 0;) {
        dated_ -= at->second.entries.size();
        at = dates_.erase(at);
    }
}

void Index::forgetTotals()
{
    if (!totals_.empty()) {
        totals_.clear();
        dirty_ = true;
    }
}

std::string Index::save(uint64_t card) const
{
    Writer writer;
    writer.bytes(Magic, sizeof Magic);
    writer.number(Version, 4);
    writer.number(card, 8);

    writer.number(totals_.size(), 4);
    for (const auto &total : totals_) {
        writer.text(total.first);
        writer.number(total.second.totals.files, 8);
        writer.number(total.second.totals.folders, 8);
        writer.number(total.second.totals.bytes, 8);
        writer.number(total.second.fingerprint, 8);
    }
    writer.number(listings_.size(), 4);
    for (const auto &listing : listings_) {
        writer.text(listing.first);
        writer.number(listing.second.hidden ? 1 : 0, 1);
        writer.number(listing.second.entries.size(), 4);
        for (const fs::Entry &entry : listing.second.entries) {
            writer.text(entry.name);
            writer.number(entry.folder ? 1 : 0, 1);
            writer.number(static_cast<uint64_t>(entry.size), 8);
            writer.number(static_cast<uint64_t>(entry.modified), 8);
        }
    }
    writer.number(dates_.size(), 4);
    for (const auto &folder : dates_) {
        writer.text(folder.first);
        writer.number(folder.second.entries.size(), 4);
        for (const auto &dated : folder.second.entries) {
            writer.text(dated.first);
            writer.number(static_cast<uint64_t>(dated.second.size), 8);
            writer.number(static_cast<uint64_t>(dated.second.modified), 8);
        }
    }
    writer.number(checksum(writer.out.data(), writer.out.size()), 8);
    return writer.out;
}

bool Index::load(const std::string &data, uint64_t card)
{
    *this = Index(maxListed_, maxTotals_, maxDated_);
    if (data.size() < sizeof Magic + 4 + 8 + 8) {
        return false;
    }
    const size_t body = data.size() - 8;
    Reader tail(data.data() + body, 8);
    uint64_t sum = 0;
    if (!tail.number(sum, 8) || sum != checksum(data.data(), body)) {
        return false;
    }

    Index loaded(maxListed_, maxTotals_, maxDated_);
    Reader reader(data.data(), body);
    char magic[sizeof Magic];
    uint64_t version = 0;
    uint64_t savedCard = 0;
    if (!reader.bytes(magic, sizeof magic) ||
        std::memcmp(magic, Magic, sizeof Magic) != 0 ||
        !reader.number(version, 4) || version != Version ||
        !reader.number(savedCard, 8) || savedCard != card) {
        return false;
    }

    uint64_t count = 0;
    if (!reader.count(count, 2 + 32)) {
        return false;
    }
    for (uint64_t index = 0; index < count; ++index) {
        std::string key;
        Total total;
        if (!reader.text(key) || !reader.number(total.totals.files, 8) ||
            !reader.number(total.totals.folders, 8) ||
            !reader.number(total.totals.bytes, 8) ||
            !reader.number(total.fingerprint, 8)) {
            return false;
        }
        if (loaded.totals_.size() < maxTotals_) {
            loaded.totals_[key] = total; // unverified until added up again
        }
    }

    if (!reader.count(count, 2 + 1 + 4)) {
        return false;
    }
    for (uint64_t index = 0; index < count; ++index) {
        std::string key;
        uint64_t hidden = 0;
        uint64_t entries = 0;
        if (!reader.text(key) || !reader.number(hidden, 1) ||
            !reader.count(entries, 2 + 1 + 16)) {
            return false;
        }
        Listing listing;
        listing.hidden = hidden != 0;
        listing.entries.resize(entries);
        for (fs::Entry &entry : listing.entries) {
            uint64_t folder = 0;
            if (!reader.text(entry.name) || !reader.number(folder, 1) ||
                !reader.signedNumber(entry.size) ||
                !reader.signedNumber(entry.modified)) {
                return false;
            }
            entry.folder = folder != 0;
        }
        if (loaded.listed_ + listing.entries.size() <= maxListed_) {
            loaded.listed_ += listing.entries.size();
            listing.used = ++loaded.clock_;
            loaded.listings_[key] = std::move(listing);
        }
    }

    if (!reader.count(count, 2 + 4)) {
        return false;
    }
    for (uint64_t index = 0; index < count; ++index) {
        std::string key;
        uint64_t entries = 0;
        if (!reader.text(key) || !reader.count(entries, 2 + 16)) {
            return false;
        }
        DatedFolder folder;
        for (uint64_t at = 0; at < entries; ++at) {
            std::string name;
            Dated dated;
            if (!reader.text(name) || !reader.signedNumber(dated.size) ||
                !reader.signedNumber(dated.modified)) {
                return false;
            }
            folder.entries[name] = dated;
        }
        if (loaded.dated_ + folder.entries.size() <= maxDated_) {
            loaded.dated_ += folder.entries.size();
            folder.used = ++loaded.clock_;
            loaded.dates_[key] = std::move(folder);
        }
    }
    if (!reader.atEnd()) {
        return false;
    }
    *this = std::move(loaded);
    dirty_ = false;
    return true;
}

void Index::adopt(Index &&loaded)
{
    for (auto &listing : loaded.listings_) {
        if (listings_.count(listing.first) == 0 &&
            listed_ + listing.second.entries.size() <= maxListed_) {
            listed_ += listing.second.entries.size();
            listing.second.used = ++clock_;
            listings_.emplace(listing.first, std::move(listing.second));
        }
    }
    for (auto &total : loaded.totals_) {
        if (totals_.size() < maxTotals_) {
            totals_.emplace(total.first, total.second); // kept if known
        }
    }
    for (auto &folder : loaded.dates_) {
        DatedFolder &into = dates_[folder.first];
        for (auto &dated : folder.second.entries) {
            if (dated_ < maxDated_ && into.entries.insert(dated).second) {
                ++dated_;
            }
        }
        into.used = std::max(into.used, ++clock_);
    }
}

void Index::dropListing(std::map<std::string, Listing>::iterator at)
{
    listed_ -= at->second.entries.size();
    listings_.erase(at);
}

void Index::dropTotalsAbove(std::string key)
{
    while (!key.empty()) {
        totals_.erase(key);
        key = parentKey(key);
    }
}

void Index::makeRoomForListing(size_t count)
{
    // The longest unused go first.
    while (listed_ + count > maxListed_ && !listings_.empty()) {
        auto oldest = listings_.begin();
        for (auto at = listings_.begin(); at != listings_.end(); ++at) {
            if (at->second.used < oldest->second.used) {
                oldest = at;
            }
        }
        dropListing(oldest);
    }
}

void Index::makeRoomForDates()
{
    // Whole folders go, the longest unused first, but never the one just
    // used.
    while (dated_ > maxDated_ && dates_.size() > 1) {
        auto oldest = dates_.begin();
        for (auto at = dates_.begin(); at != dates_.end(); ++at) {
            if (at->second.used < oldest->second.used) {
                oldest = at;
            }
        }
        dated_ -= oldest->second.entries.size();
        dates_.erase(oldest);
    }
}

} // namespace fileindex
