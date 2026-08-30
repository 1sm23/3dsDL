#pragma once

#include <string>

struct DownloadResult {
    bool success = false;
    bool cancelled = false;
    long httpStatus = 0;
    std::string message;
    std::string path;
};

DownloadResult downloadFile(const std::string &url,
                            const std::string &destination);
