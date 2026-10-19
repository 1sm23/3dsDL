#pragma once

#include <string>

namespace url {

// True for an http:// or https:// URL with a host and no whitespace or
// control characters. The scheme is matched case-insensitively because QR
// codes in alphanumeric mode can only carry upper-case letters.
bool isHttpUrl(const std::string &value);
bool isHttps(const std::string &value);

// Lower-case host name without user info or port, or "" when there is none.
std::string host(const std::string &value);

// Last path segment, still percent-encoded, without query or fragment.
std::string lastPathSegment(const std::string &value);

// Decodes %XX escapes. Malformed escapes are kept as they are.
std::string percentDecode(const std::string &value);
// Escapes everything but letters, digits and -._~ for a query value.
std::string percentEncode(const std::string &value);

} // namespace url
