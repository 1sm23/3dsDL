#include "feedback.hpp"

#include "url.hpp"

namespace feedback {

const char *modelName(int model)
{
    static const char *const Names[] = {
        "Nintendo 3DS",        "Nintendo 3DS XL",     "New Nintendo 3DS",
        "Nintendo 2DS",        "New Nintendo 3DS XL", "New Nintendo 2DS XL",
    };
    return model >= 0 && model < 6 ? Names[model] : "";
}

std::string summary(const Console &console)
{
    std::string line = "3dsDL " + console.version +
                       (console.cia ? " (CIA)" : " (3DSX)");
    if (!console.model.empty()) {
        line += " on " + console.model;
    }
    if (!console.system.empty()) {
        line += ", " + console.system;
    }
    return line;
}

std::string newIssueLink(const Console &console)
{
    // GitHub fills a form field from the query parameter named after its id
    // (.github/ISSUE_TEMPLATE/bug.yml).
    return std::string(IssuesPage) + "/new?template=bug.yml&details=" +
           url::percentEncode(summary(console));
}

std::string withoutScheme(const std::string &link)
{
    const size_t start = link.find("://");
    return start == std::string::npos ? link : link.substr(start + 3);
}

} // namespace feedback
