#include "http.hpp"
#include "metrics.hpp"
#include "rate_limiter.hpp"
#include "server.hpp"
#include "stream.hpp"
#include "util.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

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

    // Token bucket: burst up to capacity, deny when empty, refill over time.
    {
        using clock = std::chrono::steady_clock;
        aster::RateLimiter limiter(4.0, 2.0);  // burst 4, refill 2/s
        const clock::time_point t0 = clock::now();
        bool burst_ok = true;
        for (int i = 0; i < 4; ++i) {
            burst_ok = burst_ok && limiter.allow("10.0.0.1", t0);
        }
        expect_true("rl burst allowed", burst_ok);
        expect_true("rl empty denied", !limiter.allow("10.0.0.1", t0));
        expect_true("rl other ip independent", limiter.allow("10.0.0.2", t0));
        const clock::time_point t1 = t0 + std::chrono::seconds(1);  // +2 tokens
        expect_true("rl refill one", limiter.allow("10.0.0.1", t1));
        expect_true("rl refill two", limiter.allow("10.0.0.1", t1));
        expect_true("rl refill exhausted", !limiter.allow("10.0.0.1", t1));
        const clock::time_point t2 = t1 + std::chrono::seconds(60);  // cap at capacity
        bool capped_ok = true;
        for (int i = 0; i < 4; ++i) {
            capped_ok = capped_ok && limiter.allow("10.0.0.1", t2);
        }
        expect_true("rl refill capped allows", capped_ok);
        expect_true("rl refill capped denies fifth", !limiter.allow("10.0.0.1", t2));
    }

    // HTTP date formatting + parsing round-trip.
    {
        const std::time_t stamp = 1789016400;  // some fixed moment
        const std::string formatted = aster::http_date(stamp);
        std::time_t parsed = 0;
        expect_true("http_date parses", aster::parse_http_date(formatted, parsed));
        expect_true("http_date round trip", parsed == stamp);
        expect_eq("http_date known", aster::http_date(static_cast<std::time_t>(0)),
                  "Thu, 01 Jan 1970 00:00:00 GMT");
        std::time_t junk = 0;
        expect_true("http_date rejects junk", !aster::parse_http_date("not a date", junk));
    }

    // FNV-1a hex hash: stable, distinct for different inputs, 16 hex chars.
    {
        const std::string first = aster::weak_hash_hex("hello");
        expect_eq("hash stable", first, aster::weak_hash_hex("hello"));
        expect_true("hash distinct", first != aster::weak_hash_hex("world"));
        expect_true("hash length", first.size() == 16);
        expect_true("hash hex chars",
                    first.find_first_not_of("0123456789abcdef") == std::string::npos);
    }

    // Static path resolution: containment inside the public root.
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "aster_unit_public";
        fs::remove_all(root);
        fs::create_directories(root / "sub");
        std::ofstream(root / "index.html") << "<html></html>";
        std::ofstream(root / "styles.css") << "body{}";
        std::ofstream(root / "sub" / "page.html") << "<p>hi</p>";

        const auto index = aster::resolve_static_path(root, "/");
        expect_true("static root resolves index",
                    index.has_value() && index->filename() == "index.html");
        const auto css = aster::resolve_static_path(root, "/styles.css");
        expect_true("static file resolves", css.has_value() && fs::exists(*css));
        const auto nested = aster::resolve_static_path(root, "/sub/page.html");
        expect_true("static nested resolves", nested.has_value() && fs::exists(*nested));
        expect_true("static dotdot rejected",
                    !aster::resolve_static_path(root, "/../secret.txt").has_value());
        expect_true("static deep dotdot rejected",
                    !aster::resolve_static_path(root, "/sub/../../etc/passwd").has_value());
        expect_true("static nul rejected",
                    !aster::resolve_static_path(root, std::string("/a\0b", 4)).has_value());

        // A symlink inside public/ escaping the root must fail containment.
        std::error_code ec;
        fs::create_symlink("/etc", root / "escape", ec);
        if (!ec) {
            expect_true("static symlink escape rejected",
                        !aster::resolve_static_path(root, "/escape/passwd").has_value());
        }
        fs::remove_all(root);
    }

    // StreamHub: caps concurrent SSE clients at kMaxStreamClients.
    {
        aster::StreamHub hub;
        bool all_acquired = true;
        for (int i = 0; i < aster::kMaxStreamClients; ++i) {
            all_acquired = all_acquired && hub.try_acquire();
        }
        expect_true("hub cap acquires", all_acquired);
        expect_true("hub over cap denied", !hub.try_acquire());
        hub.release();
        expect_true("hub release reopens slot", hub.try_acquire());
        for (int i = 0; i < aster::kMaxStreamClients; ++i) {
            hub.release();
        }
        expect_true("hub drained", hub.active() == 0);
    }

    // Metrics telemetry snapshot: SSE frame shape and top-6 by_path trimming.
    {
        aster::Metrics metrics;
        for (int i = 0; i < 8; ++i) {
            metrics.record("/p" + std::to_string(i), 200, std::chrono::microseconds(500));
        }
        for (int i = 0; i < 5; ++i) {
            metrics.record("/hot", 200, std::chrono::microseconds(250));
        }
        const std::string frame = metrics.snapshot_json(7);
        expect_true("snapshot has tick", frame.find("\"tick\":7") != std::string::npos);
        expect_true("snapshot has totals",
                    frame.find("\"total_requests\":13") != std::string::npos);
        expect_true("snapshot has status", frame.find("\"2xx\":13") != std::string::npos);
        expect_true("snapshot has latency", frame.find("\"p99\"") != std::string::npos);
        expect_true("snapshot keeps hottest path", frame.find("\"/hot\":5") != std::string::npos);
        std::size_t path_entries = 0;
        for (std::size_t at = frame.find("\"/"); at != std::string::npos;
             at = frame.find("\"/", at + 1)) {
            ++path_entries;
        }
        expect_true("snapshot trims to top 6 paths", path_entries == 6);
        expect_true("full metrics keep every path",
                    metrics.to_json().find("\"/p7\"") != std::string::npos);
    }

    // run_sse over a socketpair: handshake + telemetry events, stops on shutdown.
    {
        int fds[2] = {-1, -1};
        expect_true("sse socketpair", ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        aster::Metrics metrics;
        metrics.record("/api/health", 200, std::chrono::microseconds(400));
        std::atomic<bool> running{true};
        aster::StreamHub hub;
        expect_true("sse hub acquire", hub.try_acquire());
        std::thread producer([&] { aster::run_sse(fds[1], metrics, running, hub); });

        std::string received;
        char buffer[4096];
        while (received.find("\n\n") == std::string::npos) {
            const ssize_t got = ::recv(fds[0], buffer, sizeof(buffer), 0);
            if (got <= 0) {
                break;
            }
            received.append(buffer, static_cast<std::size_t>(got));
        }
        running = false;
        producer.join();
        ::close(fds[0]);
        expect_true("sse handshake status",
                    received.find("HTTP/1.1 200 OK") != std::string::npos);
        expect_true("sse handshake content type",
                    received.find("Content-Type: text/event-stream") != std::string::npos);
        expect_true("sse handshake no buffering",
                    received.find("X-Accel-Buffering: no") != std::string::npos);
        expect_true("sse event name", received.find("event: telemetry") != std::string::npos);
        expect_true("sse event payload",
                    received.find("data: {\"tick\":1,") != std::string::npos);
        expect_true("sse payload totals",
                    received.find("\"total_requests\":1") != std::string::npos);
        expect_true("sse released hub", hub.active() == 0);
    }

    {
        const std::string raw =
            "GET /api/mission?seed=alpha%2Done&mode=calm HTTP/1.1\r\n"
            "HOST:  aster.local \r\n"
            "X-Custom:\ttabbed\r\n"
            "\r\n";
        const Request request = aster::parse_request_headers(raw);
        expect_eq("parse_request path", request.path, "/api/mission");
        expect_eq("parse_request query decoded", request.query.at("seed"), "alpha-one");
        expect_eq("parse_request query second", request.query.at("mode"), "calm");
        expect_eq("parse_request header lowercased", request.headers.at("host"), "aster.local ");
        expect_eq("parse_request header tab trimmed", request.headers.at("x-custom"), "tabbed");
        expect_true("parse_request no body", request.body.empty());
    }
    {
        const std::string raw =
            "POST /api/echo HTTP/1.1\r\nContent-Length: 4\r\n\r\nbody";
        const Request request = aster::parse_request_headers(raw);
        expect_eq("parse_request body split", request.body, "body");
    }
    {
        const Request request = aster::parse_request_headers("GET ?a=1 HTTP/1.1\r\n\r\n");
        expect_eq("parse_request empty path becomes root", request.path, "/");
    }

    {
        const auto params = aster::parse_query("a=1&&b&c=%2Bx&d=");
        expect_eq("parse_query plain", params.at("a"), "1");
        expect_eq("parse_query missing equals", params.at("b"), "");
        expect_eq("parse_query encoded value", params.at("c"), "+x");
        expect_eq("parse_query empty value", params.at("d"), "");
        expect_true("parse_query skips empty pairs", params.size() == 4);
        const auto encoded_key = aster::parse_query("se%2Ded=1");
        expect_eq("parse_query encoded key", encoded_key.begin()->first, "se-ed");
    }

    {
        const std::map<std::string, std::string> query{{"points", "900"}, {"junk", "abc"}};
        expect_true("int_param clamps high", aster::int_param(query, "points", 10, 1, 500) == 500);
        expect_true("int_param non-numeric fallback", aster::int_param(query, "junk", 7, 1, 500) == 7);
        expect_true("int_param missing fallback", aster::int_param(query, "absent", 42, 1, 500) == 42);
        expect_eq("string_param truncates", aster::string_param(query, "junk", "dflt", 2), "ab");
        expect_eq("string_param fallback", aster::string_param(query, "absent", "dflt"), "dflt");
    }

    expect_eq("mime html", aster::mime_type("index.html"), "text/html; charset=utf-8");
    expect_eq("mime css", aster::mime_type("styles.css"), "text/css; charset=utf-8");
    expect_eq("mime js", aster::mime_type("app.js"), "application/javascript; charset=utf-8");
    expect_eq("mime svg", aster::mime_type("logo.svg"), "image/svg+xml");
    expect_eq("mime unknown", aster::mime_type("data.bin"), "application/octet-stream");

    expect_true("stable_seed deterministic", aster::stable_seed("orion") == aster::stable_seed("orion"));
    expect_true("stable_seed distinguishes", aster::stable_seed("orion") != aster::stable_seed("lyra"));
    expect_true("stable_seed empty fnv basis", aster::stable_seed("") == 2166136261u);

    expect_eq("version constant", std::string(aster::kVersion), "2.0.0");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All unit tests passed\n";
    return 0;
}
