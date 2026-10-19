#include "../check.hpp"

#include "remote.hpp"

#include <string>
#include <vector>

TEST("remote: finds links in pasted text and FBI payloads")
{
    const std::vector<std::string> links = remote::extractLinks(
        "Get it here: https://example.com/a.cia, and (http://h/b.3dsx).\n"
        "https://example.com/a.cia\r\nftp://nope/c.zip  not-a-link\n"
        "<https://x.org/d%20e.zip>");
    CHECK_EQ(links.size(), static_cast<size_t>(3));
    CHECK_EQ(links[0], std::string("https://example.com/a.cia"));
    CHECK_EQ(links[1], std::string("http://h/b.3dsx"));
    CHECK_EQ(links[2], std::string("https://x.org/d%20e.zip"));
    CHECK(remote::extractLinks("   \n").empty());

    const unsigned char length[4] = {0x00, 0x01, 0x02, 0x03};
    CHECK_EQ(remote::payloadLength(length), static_cast<size_t>(0x010203));
}

TEST("remote: parses web requests as they arrive")
{
    remote::HttpRequest request;
    const std::string body = "links=https%3A%2F%2Fa.b%2Fc.cia+https%3A%2F%2Fd.e%2Ff";
    const std::string full = "POST /send?x=1 HTTP/1.1\r\nHost: 3ds\r\n"
                             "Content-Type: application/x-www-form-urlencoded\r\n"
                             "content-length: " +
                             std::to_string(body.size()) + "\r\n\r\n" + body;
    CHECK(remote::parseRequest(full.substr(0, 20), request) ==
          remote::Parse::Incomplete);
    CHECK(remote::parseRequest(full.substr(0, full.size() - 3), request) ==
          remote::Parse::Incomplete);
    CHECK(remote::parseRequest(full, request) == remote::Parse::Done);
    CHECK_EQ(request.method, std::string("POST"));
    CHECK_EQ(request.path, std::string("/send"));
    CHECK_EQ(request.query, std::string("x=1"));
    CHECK_EQ(request.headers["host"], std::string("3ds"));
    CHECK_EQ(remote::formValue(request.body, "links"),
             std::string("https://a.b/c.cia https://d.e/f"));
    CHECK_EQ(remote::formValue(request.body, "missing"), std::string(""));

    CHECK(remote::parseRequest("GET / HTTP/1.1\r\n\r\n", request) ==
          remote::Parse::Done);
    CHECK(remote::parseRequest("nonsense\r\n\r\n", request) ==
          remote::Parse::Bad);
    CHECK(remote::parseRequest("GET / HTTP/1.1\r\nContent-Length: x\r\n\r\n",
                               request) == remote::Parse::Bad);
    CHECK(remote::parseRequest("POST / HTTP/1.1\r\nContent-Length: 99999\r\n\r\n",
                               request, 100) == remote::Parse::Bad);
}

TEST("remote: responses and the page")
{
    const std::string response = remote::httpResponse(404, "text/plain", "no");
    CHECK(response.rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0);
    CHECK(response.find("Content-Length: 2\r\n") != std::string::npos);
    CHECK(response.find("\r\n\r\nno") != std::string::npos);

    const std::string html = remote::page("Sent <2> links", true);
    CHECK(html.find("action=\"/send\"") != std::string::npos);
    CHECK(html.find("Sent &lt;2&gt; links") != std::string::npos);
    CHECK(html.find("class=\"note good\"") != std::string::npos);
    CHECK(remote::page().find("class=\"note") == std::string::npos);
}
