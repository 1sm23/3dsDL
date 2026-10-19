#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// Receiving links over the local network. Two ways in:
//
// - FBI's remote install protocol on port 5000, which servefiles.py,
//   sendurls.py and Boop speak: a 4-byte big-endian length, then that many
//   bytes of URLs separated by newlines. The sender keeps the connection
//   open and waits for one byte back, sent once everything has downloaded;
//   servefiles.py stops serving its files when it arrives.
// - A small web page, so a phone or computer on the same Wi-Fi can paste
//   links into a form.
//
// Only parsing and page text live here; net/remote_server has the sockets.
namespace remote {

constexpr unsigned short FbiPort = 5000;
// The FBI tools send a few URLs; anything bigger is not them.
constexpr size_t MaxPayload = 256 * 1024;

// The http(s) links in text, in order and without repeats. Anything
// separated by whitespace is considered, so a pasted list, FBI's newline
// payload and a sentence with a link in it all work.
std::vector<std::string> extractLinks(const std::string &text);

// The length FBI's protocol puts first, from its 4 bytes.
size_t payloadLength(const unsigned char *bytes);

struct HttpRequest {
    std::string method; // "GET"
    std::string path;   // "/send", without the query
    std::string query;  // after '?', still encoded
    std::map<std::string, std::string> headers; // names in lower case
    std::string body;
};

enum class Parse { Incomplete, Done, Bad };

// Reads a request from the bytes received so far. Done once the head and a
// body of Content-Length bytes have arrived; Bad for anything malformed or
// over maxBody.
Parse parseRequest(const std::string &data, HttpRequest &out,
                   size_t maxBody = 64 * 1024);

// The value of field in an application/x-www-form-urlencoded string, decoded
// ('+' is a space, %XX a byte), or "" when it is missing.
std::string formValue(const std::string &form, const std::string &field);

// A whole response with Connection: close.
std::string httpResponse(int status, const std::string &contentType,
                         const std::string &body);

// The page a phone sees: a box to paste links into. message, if any, shows
// above it, e.g. after links were sent.
std::string page(const std::string &message = "", bool good = true);

} // namespace remote
