#include "remote.hpp"

#include "text.hpp"
#include "url.hpp"

#include <cctype>
#include <cstdlib>
#include <set>

namespace remote {

namespace {

int hexValue(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string formDecode(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        const char c = value[index];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && index + 2 < value.size() &&
                   hexValue(value[index + 1]) >= 0 &&
                   hexValue(value[index + 2]) >= 0) {
            out += static_cast<char>(hexValue(value[index + 1]) * 16 +
                                     hexValue(value[index + 2]));
            index += 2;
        } else {
            out += c;
        }
    }
    return out;
}

std::string escapeHtml(const std::string &value)
{
    std::string out;
    for (const char c : value) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

const char *statusText(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 303:
        return "See Other";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    default:
        return "Error";
    }
}

} // namespace

std::vector<std::string> extractLinks(const std::string &value)
{
    std::vector<std::string> links;
    std::set<std::string> seen;
    size_t start = 0;
    while (start < value.size()) {
        while (start < value.size() &&
               std::isspace(static_cast<unsigned char>(value[start]))) {
            ++start;
        }
        size_t end = start;
        while (end < value.size() &&
               !std::isspace(static_cast<unsigned char>(value[end]))) {
            ++end;
        }
        std::string word = value.substr(start, end - start);
        start = end;
        // A link at the end of a sentence or in brackets.
        while (!word.empty() && std::string(".,;)>]'\"").find(word.back()) !=
                                    std::string::npos) {
            word.pop_back();
        }
        while (!word.empty() && std::string("(<['\"").find(word.front()) !=
                                    std::string::npos) {
            word.erase(0, 1);
        }
        if (url::isHttpUrl(word) && seen.insert(word).second) {
            links.push_back(word);
        }
    }
    return links;
}

size_t payloadLength(const unsigned char *bytes)
{
    return static_cast<size_t>(bytes[0]) << 24 |
           static_cast<size_t>(bytes[1]) << 16 |
           static_cast<size_t>(bytes[2]) << 8 | static_cast<size_t>(bytes[3]);
}

Parse parseRequest(const std::string &data, HttpRequest &out, size_t maxBody)
{
    const size_t headEnd = data.find("\r\n\r\n");
    if (headEnd == std::string::npos) {
        return data.size() > 16 * 1024 ? Parse::Bad : Parse::Incomplete;
    }
    out = HttpRequest();
    const std::string head = data.substr(0, headEnd);
    size_t lineEnd = head.find("\r\n");
    const std::string requestLine = head.substr(0, lineEnd);
    const size_t firstSpace = requestLine.find(' ');
    const size_t secondSpace = requestLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos ||
        requestLine.compare(secondSpace + 1, 5, "HTTP/") != 0) {
        return Parse::Bad;
    }
    out.method = requestLine.substr(0, firstSpace);
    const std::string target =
        requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    const size_t question = target.find('?');
    out.path = target.substr(0, question);
    if (question != std::string::npos) {
        out.query = target.substr(question + 1);
    }
    if (out.path.empty() || out.path[0] != '/') {
        return Parse::Bad;
    }

    while (lineEnd != std::string::npos) {
        const size_t start = lineEnd + 2;
        lineEnd = head.find("\r\n", start);
        const std::string line = head.substr(
            start, lineEnd == std::string::npos ? std::string::npos
                                                : lineEnd - start);
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        out.headers[text::lower(text::trim(line.substr(0, colon)))] =
            text::trim(line.substr(colon + 1));
    }

    size_t length = 0;
    const auto found = out.headers.find("content-length");
    if (found != out.headers.end()) {
        const std::string &value = found->second;
        if (value.empty() ||
            value.find_first_not_of("0123456789") != std::string::npos ||
            value.size() > 9) {
            return Parse::Bad;
        }
        length = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
    }
    if (length > maxBody) {
        return Parse::Bad;
    }
    const size_t bodyStart = headEnd + 4;
    if (data.size() < bodyStart + length) {
        return Parse::Incomplete;
    }
    out.body = data.substr(bodyStart, length);
    return Parse::Done;
}

std::string formValue(const std::string &form, const std::string &field)
{
    size_t start = 0;
    while (start <= form.size()) {
        size_t end = form.find('&', start);
        if (end == std::string::npos) {
            end = form.size();
        }
        const std::string pair = form.substr(start, end - start);
        const size_t equals = pair.find('=');
        if (formDecode(pair.substr(0, equals)) == field) {
            return equals == std::string::npos
                       ? ""
                       : formDecode(pair.substr(equals + 1));
        }
        start = end + 1;
    }
    return "";
}

std::string httpResponse(int status, const std::string &contentType,
                         const std::string &body)
{
    std::string response = "HTTP/1.1 " + std::to_string(status) + " " +
                           statusText(status) + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "Connection: close\r\n\r\n";
    return response + body;
}

std::string page(const std::string &message, bool good)
{
    std::string html =
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Send to 3dsDL</title><style>"
        ":root{color-scheme:light}"
        "body{margin:0;font:16px/1.45 system-ui,-apple-system,sans-serif;"
        "background:linear-gradient(#f2ebb2,#f0e694);color:#4f493b;"
        "min-height:100vh}"
        "main{max-width:34rem;margin:0 auto;padding:24px 16px}"
        "h1{margin:0 0 4px;font-size:1.5rem;color:#1c93c2}"
        "p{margin:0 0 16px;color:#615a4b}"
        "form{background:#dcd49a;border-radius:12px;padding:14px}"
        "textarea{box-sizing:border-box;width:100%;min-height:9rem;"
        "border:1px solid #b3aa83;border-radius:8px;padding:10px;"
        "font:15px/1.4 ui-monospace,monospace;background:#fffdf4;"
        "color:#141414;resize:vertical}"
        "button{margin-top:12px;width:100%;padding:12px;border:1px solid #8f8659;"
        "border-radius:10px;font:600 17px system-ui,sans-serif;color:#615a4b;"
        "background:linear-gradient(#fefcf5,#e0dccb);cursor:pointer}"
        "button:active{background:#e0dccb}"
        ".note{border-radius:10px;padding:10px 12px;margin-bottom:14px;"
        "color:#fff}"
        ".good{background:#3fa65a}.bad{background:#e0301a}"
        "small{display:block;margin-top:14px;color:#8c8466}"
        "</style></head><body><main>"
        "<h1>Send to 3dsDL</h1>"
        "<p>Paste one or more direct download links. They go to the 3DS, "
        "which downloads them one after another.</p>";
    if (!message.empty()) {
        html += std::string("<div class=\"note ") + (good ? "good" : "bad") +
                "\">" + escapeHtml(message) + "</div>";
    }
    html += "<form method=\"post\" action=\"/send\">"
            "<textarea name=\"links\" placeholder=\"https://...\" "
            "autofocus required></textarea>"
            "<button type=\"submit\">Send to 3DS</button></form>"
            "<small>Only links are sent. Keep 3dsDL on the Receive screen "
            "while you send.</small></main></body></html>";
    return html;
}

} // namespace remote
