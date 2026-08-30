#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <curl/curl.h>

#include <string>

namespace ui {

enum class HomeAction {
    None,
    ScanQr,
    EnterUrl,
    ChangeFolder,
    Exit
};

enum class ConflictChoice {
    Rename,
    Overwrite,
    Cancel
};

struct DownloadView {
    std::string url;
    std::string filename;
    std::string destination;
    std::string status;
    curl_off_t downloaded = 0;
    curl_off_t total = 0;
};

bool init();
void shutdown();

HomeAction homeFrame(const std::string &destination);
bool confirmUrl(const std::string &url);
void showDialog(const std::string &title, const std::string &message,
                bool error = false);
void renderDownload(const DownloadView &view);
ConflictChoice askFileConflict(const std::string &filename,
                               const std::string &suggestedName);
void renderScannerFrame(C2D_Image preview, const std::string &status);

} // namespace ui
