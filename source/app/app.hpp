#pragma once

#include "cia.hpp"
#include "download_engine.hpp"
#include "file_types.hpp"
#include "platform.hpp"
#include "records.hpp"
#include "update.hpp"

#include <3ds.h>

#include <string>
#include <vector>

// The screens of 3dsDL and the flow between them.
class App {
public:
    App();
    ~App();
    void run();

private:
    enum class Action {
        None,
        Scan,
        EnterUrl,
        History,
        Files,
        Folder,
        Settings,
        Exit
    };

    Action home();
    void refreshStatus(bool force);

    void startDownload(const std::string &address, std::string folder);
    bool confirmDownload(const std::string &address, std::string &folder);
    void finishDownload(const std::string &address, const std::string &folder,
                        const net::Result &result);
    void offerExtraction(const std::string &zipPath, const std::string &folder,
                         const std::string &summary,
                         const std::string &title =
                             "Download complete. Extract the archive?");

    // Asks what to do with a CIA that just finished downloading.
    void offerInstall(const std::string &path, const std::string &address,
                      const std::string &summary);
    // Installs the CIA at path after the checks and questions that come
    // first. address is the download it came from, or "" for a file picked
    // on the SD card. Returns true when it was installed.
    // quiet skips the success screen, for installing several in a row.
    bool installCia(const std::string &path, const cia::Info &info,
                    bool deleteAfter, const std::string &address,
                    bool quiet = false);
    void markInstalled(const std::string &address, const std::string &path,
                       const std::string &message);

    // Files, in files.cpp. Paths of entries never end with '/'; folders
    // being browsed always do.
    void filesScreen();
    void openFile(const std::string &path);
    void ciaOptions(const std::string &path);
    // Options for the marked entries, or the highlighted one. Returns true
    // when the folder's contents may have changed.
    bool entryOptions(const std::string &folder,
                      const std::vector<std::string> &paths,
                      std::vector<std::string> &marked,
                      const std::vector<std::string> &all);
    bool folderOptions(const std::string &folder,
                       const std::vector<std::string> &entries,
                       std::vector<std::string> &marked);
    void textEditor(const std::string &path);
    void hexViewer(const std::string &path);
    void pictureViewer(const std::vector<std::string> &pictures,
                       size_t index);
    void zipViewer(const std::string &path);
    void titleDetails(const std::string &path, filetype::Kind kind);
    void showProperties(const std::vector<std::string> &paths);
    bool renameEntry(const std::string &path);
    bool deleteEntries(const std::vector<std::string> &paths);
    bool pasteInto(const std::string &folder);
    bool createFolder(const std::string &folder);
    bool createFile(const std::string &folder);
    void installCias(const std::vector<std::string> &paths, bool deleteAfter);

    bool chooseFolder(std::string &folder);
    bool browseFolders(std::string &folder);
    bool typeFolder(std::string &folder);
    void changeDefaultFolder();

    void historyScreen();
    bool confirmClearHistory();
    void settingsScreen();
    void aboutScreen();

    // Software updates, in software_update.cpp.
    void softwareUpdateScreen();
    void showReleaseNotes(const update::Release &release);
    void installUpdate(const update::Release &release);
    // Runs every frame: notices when the background check finishes and
    // shows the notification once.
    void pollUpdate();

    // Compares the console clock with internet time and offers to set it.
    // Returns true when the clock was set.
    bool clockScreen();
    // utcNowSeconds < 0 when the real time is unknown.
    bool chooseTimeZone(int64_t utcNowSeconds);

    void record(HistoryEntry entry);
    void persistSettings();

    Settings settings_;
    std::vector<HistoryEntry> history_;
    int homeSelection_ = 1; // Scan QR Code
    std::string filesFolder_;
    // Copied or cut entries waiting to be pasted.
    std::vector<std::string> clipboard_;
    bool clipboardCut_ = false;
    int64_t freeBytes_ = -1;
    platform::Wifi wifi_ = platform::Wifi::Unknown;
    u64 lastRefresh_ = 0;
    bool updateAvailable_ = false;
    bool updateNoticeShown_ = false;
    uint32_t updateGeneration_ = 0;
    // Set when 3dsDL should close, e.g. to restart into an update.
    bool quit_ = false;
};
