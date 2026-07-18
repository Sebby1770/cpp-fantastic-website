#include "http.hpp"
#include "util.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_eq(const std::string& name, const std::string& got, const std::string& want) {
    if (got != want) {
        std::cerr << "FAIL " << name << ": got \"" << got << "\" want \"" << want << "\"\n";
        ++failures;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

void expect_true(const std::string& name, bool condition) {
    if (!condition) {
        std::cerr << "FAIL " << name << "\n";
        ++failures;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

std::map<std::string, std::string> headers_with_length(const std::string& value) {
    return {{"content-length", value}};
}

}  // namespace

int main() {
    using aster::ContentLengthClass;
    using aster::ParseState;
    using aster::Request;
    using aster::classify_content_length;
    using aster::json_escape;
    using aster::try_parse_request;
    using aster::url_decode;
    using aster::wants_keep_alive;

    expect_eq("url plain", url_decode("hello"), "hello");
    expect_eq("url plus", url_decode("a+b"), "a b");
    expect_eq("url percent", url_decode("seed%2Dtest"), "seed-test");
    expect_eq("url space hex", url_decode("hi%20there"), "hi there");
    expect_eq("url mixed", url_decode("a%2Fb+c"), "a/b c");

    expect_eq("json plain", json_escape("hello"), "hello");
    expect_eq("json quote", json_escape("say \"hi\""), "say \\\"hi\\\"");
    expect_eq("json slash", json_escape("a\\b"), "a\\\\b");
    expect_eq("json newline", json_escape("a\nb"), "a\\nb");
    expect_eq("json tab", json_escape("a\tb"), "a\\tb");

    // Content-Length classification against the body limit.
    {
        const auto absent = classify_content_length({}, 1024);
        expect_true("cl absent ok", absent.status == ContentLengthClass::Ok && absent.length == 0);
        const auto small = classify_content_length(headers_with_length("10"), 1024);
        expect_true("cl small ok", small.status == ContentLengthClass::Ok && small.length == 10);
        const auto exact = classify_content_length(headers_with_length("1024"), 1024);
        expect_true("cl exact ok", exact.status == ContentLengthClass::Ok && exact.length == 1024);
        const auto large = classify_content_length(headers_with_length("2097152"), 1024 * 1024);
        expect_true("cl too large", large.status == ContentLengthClass::TooLarge);
        const auto negative = classify_content_length(headers_with_length("-5"), 1024);
        expect_true("cl negative malformed", negative.status == ContentLengthClass::Malformed);
        const auto garbage = classify_content_length(headers_with_length("12abc"), 1024);
        expect_true("cl garbage malformed", garbage.status == ContentLengthClass::Malformed);
        const auto empty = classify_content_length(headers_with_length(""), 1024);
        expect_true("cl empty malformed", empty.status == ContentLengthClass::Malformed);
    }

    // Keep-alive decision per HTTP version + Connection header.
    expect_true("ka 1.1 default", wants_keep_alive("HTTP/1.1", ""));
    expect_true("ka 1.1 close", !wants_keep_alive("HTTP/1.1", "close"));
    expect_true("ka 1.1 close case", !wants_keep_alive("HTTP/1.1", "Close"));
    expect_true("ka 1.0 default", !wants_keep_alive("HTTP/1.0", ""));
    expect_true("ka 1.0 keep", wants_keep_alive("HTTP/1.0", "keep-alive"));
    expect_true("ka 1.0 keep case", wants_keep_alive("HTTP/1.0", "Keep-Alive"));
    expect_true("ka bogus version", !wants_keep_alive("HTTP/2.0", ""));

    // Pipelined parsing: leftover carries bytes belonging to the next request.
    {
        Request request;
        std::string leftover;
        const std::string raw =
            "GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\nHost: x\r\n\r\n";
        const ParseState state = try_parse_request(raw, request, leftover);
        expect_true("pipeline complete", state == ParseState::Complete);
        expect_eq("pipeline first path", request.path, "/a");
        expect_eq("pipeline leftover", leftover, "GET /b HTTP/1.1\r\nHost: x\r\n\r\n");

        Request second;
        std::string leftover2;
        expect_true("pipeline second complete",
                    try_parse_request(leftover, second, leftover2) == ParseState::Complete);
        expect_eq("pipeline second path", second.path, "/b");
        expect_eq("pipeline no more leftover", leftover2, "");
    }
    {
        Request request;
        std::string leftover;
        const std::string raw =
            "POST /api/echo HTTP/1.1\r\nContent-Length: 5\r\n\r\nhelloGET /next HTTP/1.1\r\n\r\n";
        const ParseState state = try_parse_request(raw, request, leftover);
        expect_true("pipeline body complete", state == ParseState::Complete);
        expect_eq("pipeline body", request.body, "hello");
        expect_eq("pipeline body leftover", leftover, "GET /next HTTP/1.1\r\n\r\n");
    }
    {
        Request request;
        std::string leftover;
        expect_true("parse need more headers",
                    try_parse_request("GET / HTTP/1.1\r\nHost:", request, leftover) ==
                        ParseState::NeedMore);
        expect_true("parse need more body",
                    try_parse_request("POST / HTTP/1.1\r\nContent-Length: 9\r\n\r\nhi",
                                      request, leftover) == ParseState::NeedMore);
        expect_true("parse malformed line",
                    try_parse_request("BLAH\r\n\r\n", request, leftover) == ParseState::Malformed);
        expect_true("parse malformed version",
                    try_parse_request("GET / HTTP/9.9\r\n\r\n", request, leftover) ==
                        ParseState::Malformed);
        expect_true("parse body too large",
                    try_parse_request("POST / HTTP/1.1\r\nContent-Length: 99\r\n\r\n",
                                      request, leftover, 10) == ParseState::BodyTooLarge);
        const std::string huge_headers = "GET / HTTP/1.1\r\nX-Pad: " +
                                         std::string(aster::kMaxHeaderBytes + 16, 'a') + "\r\n\r\n";
        expect_true("parse headers too large",
                    try_parse_request(huge_headers, request, leftover) ==
                        ParseState::HeadersTooLarge);
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All unit tests passed\n";
    return 0;
}
