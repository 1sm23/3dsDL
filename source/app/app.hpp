#pragma once

#include "download_engine.hpp"
#include "platform.hpp"
#include "records.hpp"

#include <3ds.h>

#include <string>
#include <vector>

// The screens of 3dsDL and the flow between them.
class App {
public:
    App();
    void run();

private:
    enum class Action { None, Scan, EnterUrl, History, Folder, Settings, Exit };

    Action home();
    void drawHome();
    void refreshStatus(bool force);

    void startDownload(const std::string &address, std::string folder);
    bool confirmDownload(const std::string &address, std::string &folder);
    void finishDownload(const std::string &address, const std::string &folder,
                        const net::Result &result);
    void offerExtraction(const std::string &zipPath, const std::string &folder,
                         const std::string &summary);

    bool chooseFolder(std::string &folder);
    bool browseFolders(std::string &folder);
    bool typeFolder(std::string &folder);
    void changeDefaultFolder();

    void historyScreen();
    bool confirmClearHistory();
    void settingsScreen();
    void aboutScreen();

    // Compares the console clock with internet time and offers to set it.
    // Returns true when the clock was set.
    bool clockScreen();
    // utcNowSeconds < 0 when the real time is unknown.
    bool chooseTimeZone(int64_t utcNowSeconds);

    void record(HistoryEntry entry);
    void persistSettings();

    Settings settings_;
    std::vector<HistoryEntry> history_;
    int homeSelection_ = 0;
    int64_t freeBytes_ = -1;
    platform::Wifi wifi_ = platform::Wifi::Unknown;
    u64 lastRefresh_ = 0;
};
