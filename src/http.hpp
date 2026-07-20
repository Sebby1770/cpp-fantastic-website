#pragma once

#include "util.hpp"

#include <cerrno>
#include <chrono>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aster {

inline constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
inline constexpr std::size_t kMaxBodyBytes = 1024 * 1024;
inline constexpr int kMaxRequestsPerConn = 100;
// Longest silence tolerated while waiting for (more of) a request. An idle
// keep-alive connection is closed quietly; a stalled partial request gets 408.
inline constexpr std::chrono::milliseconds kIdleTimeoutDefault{5000};
// Wall-clock budget for reading one complete request. Unlike SO_RCVTIMEO
// (which a slow-drip client resets with every byte), this bounds the whole
// read, so a slowloris-style client cannot pin a pool worker indefinitely.
inline constexpr std::chrono::milliseconds kRequestDeadlineDefault{10000};

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string version;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct Response {
    int status = 200;
    std::string status_text = "OK";
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
    bool include_body = true;
    std::vector<std::pair<std::string, std::string>> extra_headers;
};

// Outcome of reading one request off a connection.
enum class ReadResult { Ok, Closed, Timeout, Malformed, HeadersTooLarge, BodyTooLarge };

// Outcome of attempting to parse one request from an in-memory buffer.
enum class ParseState { NeedMore, Complete, Malformed, HeadersTooLarge, BodyTooLarge };

// Classification of a Content-Length header against the configured body limit.
enum class ContentLengthClass { Ok, TooLarge, Malformed };

struct ContentLengthInfo {
    ContentLengthClass status = ContentLengthClass::Ok;
    std::size_t length = 0;
};

inline bool send_all(int socket_fd, const std::string& payload) {
    const char* data = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const ssize_t sent = ::send(socket_fd, data, remaining, 0);
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

// Strict Content-Length parsing: absent means "no body"; anything non-numeric,
// negative, or with trailing garbage is Malformed; larger than max_bytes is
// TooLarge (the body must be rejected without reading it).
inline ContentLengthInfo classify_content_length(const std::map<std::string, std::string>& headers,
                                                 std::size_t max_bytes) {
    const auto it = headers.find("content-length");
    if (it == headers.end()) {
        return {ContentLengthClass::Ok, 0};
    }
    const std::string& value = it->second;
    std::string trimmed = value;
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }
    if (trimmed.empty() || trimmed.size() > 19) {
        return {ContentLengthClass::Malformed, 0};
    }
    unsigned long long parsed = 0;
    for (const char ch : trimmed) {
        if (ch < '0' || ch > '9') {
            return {ContentLengthClass::Malformed, 0};
        }
        parsed = parsed * 10 + static_cast<unsigned long long>(ch - '0');
    }
    if (parsed > max_bytes) {
        return {ContentLengthClass::TooLarge, static_cast<std::size_t>(parsed)};
    }
    return {ContentLengthClass::Ok, static_cast<std::size_t>(parsed)};
}

// Keep-alive decision per HTTP/1.x semantics (case-insensitive header value).
inline bool wants_keep_alive(const std::string& version, const std::string& connection_header) {
    const std::string connection = to_lower(connection_header);
    if (version == "HTTP/1.1") {
        return connection != "close";
    }
    if (version == "HTTP/1.0") {
        return connection == "keep-alive";
    }
    return false;
}

// Parse request-line + headers from the raw buffer; body may still be incomplete.
inline Request parse_request_headers(const std::string& raw) {
    Request request;
    const std::size_t header_end = raw.find("\r\n\r\n");
    const std::string header_block = header_end == std::string::npos ? raw : raw.substr(0, header_end);

    std::istringstream stream(header_block);
    stream >> request.method >> request.target >> request.version;

    const std::size_t query_start = request.target.find('?');
    request.path = url_decode(request.target.substr(0, query_start));
    if (query_start != std::string::npos) {
        request.query = parse_query(request.target.substr(query_start + 1));
    }
    if (request.path.empty()) {
        request.path = "/";
    }

    std::string line;
    std::getline(stream, line);  // consume rest of request line
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = to_lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        request.headers[key] = value;
    }

    if (header_end != std::string::npos && header_end + 4 < raw.size()) {
        request.body = raw.substr(header_end + 4);
    }
    return request;
}

inline bool request_line_valid(const Request& request) {
    return !request.method.empty() && !request.target.empty() &&
           request.version.size() == 8 && request.version.compare(0, 7, "HTTP/1.") == 0 &&
           request.version[7] >= '0' && request.version[7] <= '9';
}

// Pure parser over an in-memory buffer. On Complete, `request` holds exactly one
// request and `leftover` receives any pipelined bytes that followed it.
inline ParseState try_parse_request(const std::string& raw, Request& request, std::string& leftover,
                                    std::size_t max_body_bytes = kMaxBodyBytes) {
    const std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return raw.size() > kMaxHeaderBytes ? ParseState::HeadersTooLarge : ParseState::NeedMore;
    }
    if (header_end > kMaxHeaderBytes) {
        return ParseState::HeadersTooLarge;
    }

    request = parse_request_headers(raw);
    if (!request_line_valid(request)) {
        return ParseState::Malformed;
    }

    const ContentLengthInfo info = classify_content_length(request.headers, max_body_bytes);
    if (info.status == ContentLengthClass::Malformed) {
        return ParseState::Malformed;
    }
    if (info.status == ContentLengthClass::TooLarge) {
        return ParseState::BodyTooLarge;
    }

    if (request.body.size() < info.length) {
        return ParseState::NeedMore;
    }
    if (request.body.size() > info.length) {
        leftover = request.body.substr(info.length);
        request.body.resize(info.length);
    }
    return ParseState::Complete;
}

// Read one full HTTP request (headers + Content-Length body) off the socket.
// `leftover` carries unconsumed bytes between pipelined keep-alive requests:
// it seeds this read and receives any over-read bytes for the next one.
//
// Two wall-clock limits bound the read (both enforced with poll(), so a
// client dripping bytes cannot reset them the way it can reset SO_RCVTIMEO):
//   - idle_timeout: max silence between bytes. Expiring with no request bytes
//     is a quiet keep-alive close (Closed); mid-request it is a Timeout (408).
//   - request_deadline: max total time for the whole request, regardless of
//     how steadily bytes trickle in. Always Timeout when bytes were received.
inline ReadResult read_http_request(
    int client_fd, Request& request, std::string& leftover,
    std::size_t max_body_bytes = kMaxBodyBytes,
    std::chrono::milliseconds idle_timeout = kIdleTimeoutDefault,
    std::chrono::milliseconds request_deadline = kRequestDeadlineDefault) {
    using clock = std::chrono::steady_clock;
    std::string raw = std::move(leftover);
    leftover.clear();
    char buffer[4096];
    const clock::time_point start = clock::now();

    while (true) {
        switch (try_parse_request(raw, request, leftover, max_body_bytes)) {
            case ParseState::Complete: return ReadResult::Ok;
            case ParseState::Malformed: return ReadResult::Malformed;
            case ParseState::HeadersTooLarge: return ReadResult::HeadersTooLarge;
            case ParseState::BodyTooLarge: return ReadResult::BodyTooLarge;
            case ParseState::NeedMore: break;
        }

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start);
        if (elapsed >= request_deadline) {
            return raw.empty() ? ReadResult::Closed : ReadResult::Timeout;
        }
        std::chrono::milliseconds wait = request_deadline - elapsed;
        if (idle_timeout < wait) {
            wait = idle_timeout;
        }

        pollfd read_poll{};
        read_poll.fd = client_fd;
        read_poll.events = POLLIN;
        const int ready = ::poll(&read_poll, 1, static_cast<int>(wait.count()));
        if (ready == 0) {
            // Silence for the whole window: idle keep-alive expiry (quiet
            // close) or a stalled partial request (408 material).
            return raw.empty() ? ReadResult::Closed : ReadResult::Timeout;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ReadResult::Closed;
        }

        const ssize_t received = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (received == 0) {
            return ReadResult::Closed;
        }
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // spurious wakeup; the deadline math above still governs
            }
            return ReadResult::Closed;
        }
        raw.append(buffer, static_cast<std::size_t>(received));
    }
}

inline Response json_response(const std::string& body, int status = 200, const std::string& status_text = "OK") {
    return Response{status, status_text, "application/json; charset=utf-8", body, true, {}};
}

inline Response not_found() {
    return Response{404, "Not Found", "text/plain; charset=utf-8", "404 Not Found\n", true, {}};
}

inline Response bad_request(const std::string& message) {
    return Response{400, "Bad Request", "text/plain; charset=utf-8", message + "\n", true, {}};
}

inline std::string status_text_for(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

inline Response method_not_allowed() {
    Response response{405, status_text_for(405), "text/plain; charset=utf-8",
                      "405 Method Not Allowed\n", true, {}};
    response.extra_headers.emplace_back("Allow", "GET, HEAD, POST, OPTIONS");
    return response;
}

inline Response simple_status(int status) {
    return Response{status, status_text_for(status), "text/plain; charset=utf-8",
                    std::to_string(status) + " " + status_text_for(status) + "\n", true, {}};
}

inline bool send_response(int client_fd, Response response, bool keep_alive) {
    const std::size_t body_size = response.body.size();
    std::ostringstream payload;
    payload << "HTTP/1.1 " << response.status << " " << response.status_text << "\r\n";
    payload << "Content-Type: " << response.content_type << "\r\n";
    payload << "Content-Length: " << body_size << "\r\n";
    payload << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n";
    payload << "X-Content-Type-Options: nosniff\r\n";
    payload << "Access-Control-Allow-Origin: *\r\n";
    payload << "Access-Control-Allow-Methods: GET, HEAD, POST, OPTIONS\r\n";
    payload << "Access-Control-Allow-Headers: Content-Type\r\n";
    for (const auto& header : response.extra_headers) {
        payload << header.first << ": " << header.second << "\r\n";
    }
    payload << "\r\n";
    if (response.include_body) {
        payload << response.body;
    }
    return send_all(client_fd, payload.str());
}

}  // namespace aster
