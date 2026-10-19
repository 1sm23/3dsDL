#include "../check.hpp"

#include "feedback.hpp"
#include "url.hpp"

extern "C" {
#include "../../source/qrcodegen/qrcodegen.h"
}

#include <string>

namespace {

// The Report an Issue screen draws its codes at up to version 7, the most
// that fits the top screen's panel at 3 pixels a module.
bool fitsOnScreen(const std::string &link)
{
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(7)];
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(7)];
    return qrcodegen_encodeText(link.c_str(), temp, code, qrcodegen_Ecc_LOW,
                                qrcodegen_VERSION_MIN, 7, qrcodegen_Mask_AUTO,
                                true);
}

} // namespace

TEST("feedback: model names follow the CFG numbering")
{
    CHECK_EQ(std::string(feedback::modelName(0)), std::string("Nintendo 3DS"));
    CHECK_EQ(std::string(feedback::modelName(4)),
             std::string("New Nintendo 3DS XL"));
    CHECK_EQ(std::string(feedback::modelName(5)),
             std::string("New Nintendo 2DS XL"));
    CHECK_EQ(std::string(feedback::modelName(6)), std::string(""));
    CHECK_EQ(std::string(feedback::modelName(-1)), std::string(""));
}

TEST("feedback: the summary leaves out what is unknown")
{
    feedback::Console console;
    console.version = "0.7.1";
    console.cia = true;
    console.model = "New Nintendo 3DS XL";
    console.system = "11.17.0-50U";
    CHECK_EQ(feedback::summary(console),
             std::string("3dsDL 0.7.1 (CIA) on New Nintendo 3DS XL, "
                         "11.17.0-50U"));
    console.cia = false;
    console.model.clear();
    console.system.clear();
    CHECK_EQ(feedback::summary(console), std::string("3dsDL 0.7.1 (3DSX)"));
}

TEST("feedback: the new issue link carries the summary")
{
    feedback::Console console;
    console.version = "0.7.1";
    console.model = "Nintendo 2DS";
    const std::string link = feedback::newIssueLink(console);
    const std::string start = std::string(feedback::IssuesPage) +
                              "/new?template=bug.yml&details=";
    CHECK_EQ(link.compare(0, start.size(), start), 0);
    CHECK_EQ(url::percentDecode(link.substr(start.size())),
             std::string("3dsDL 0.7.1 (3DSX) on Nintendo 2DS"));
}

TEST("feedback: every link fits a QR code on the top screen")
{
    feedback::Console longest;
    longest.version = "10.12.123";
    longest.cia = false;
    longest.model = "New Nintendo 3DS XL";
    longest.system = "11.17.0-50U";
    CHECK(fitsOnScreen(feedback::newIssueLink(longest)));
    CHECK(fitsOnScreen(feedback::DiscordInvite));
    CHECK(fitsOnScreen(feedback::DeveloperPage));
    CHECK_EQ(feedback::withoutScheme(feedback::DeveloperPage),
             std::string("x.com/ankushKun_"));
    CHECK_EQ(feedback::withoutScheme("plain"), std::string("plain"));
}
