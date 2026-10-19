#pragma once

#include <string>

// Where people report problems with 3dsDL or reach its developer, and the
// details about their console that a report needs. The Report an Issue
// screen shows these links as QR codes to open on a phone.
namespace feedback {

constexpr const char *IssuesPage = "https://github.com/ankushKun/3dsDL/issues";
constexpr const char *DiscordInvite = "https://discord.gg/axrWTjyAWG";
constexpr const char *DeveloperPage = "https://x.com/ankushKun_";
constexpr const char *DeveloperHandle = "@ankushKun_";

struct Console {
    std::string version; // "0.7.1"
    bool cia = false;    // the installed CIA rather than the .3dsx
    std::string model;   // "New Nintendo 3DS XL", or "" when unknown
    std::string system;  // "11.17.0-50U", or "" when unknown
};

// The name of a model as the CFG service numbers them (CFG_SystemModel), or
// "" for a number it does not know.
const char *modelName(int model);

// One line for a report: "3dsDL 0.7.1 (CIA) on New Nintendo 3DS XL,
// 11.17.0-50U". Unknown parts are left out.
std::string summary(const Console &console);

// Opens the bug report form with the summary in its "3dsDL version and
// console" field.
std::string newIssueLink(const Console &console);

// link without "https://", for showing: "x.com/ankushKun_".
std::string withoutScheme(const std::string &link);

} // namespace feedback
