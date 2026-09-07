#pragma once

#include "util.hpp"

#include <cerrno>
#include <cstring>
#include <map>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <utility>
#include <vector>

namespace aster {

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string version = "HTTP/1.1";
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string ip = "127.0.0.1";
};

struct Response {
    int status = 200;
    std::string status_text = "OK";
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> extra_headers;
    bool close = false;
    bool take_socket = false;
};

inline std::string status_text_for(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 416: return "Range Not Satisfiable";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

inline std::string header_get(const Request& request, const std::string& key) {
    const auto found = request.headers.find(to_lower(key));
    return found == request.headers.end() ? "" : found->second;
}

inline bool wants_close(const Request& request) {
    const std::string connection = to_lower(header_get(request, "connection"));
    if (connection.find("close") != std::string::npos) {
        return true;
    }
    if (request.version == "HTTP/1.0") {
        return connection.find("keep-alive") == std::string::npos;
    }
    return false;
}

inline void add_security_headers(Response& response) {
    response.extra_headers.emplace_back("X-Content-Type-Options", "nosniff");
    response.extra_headers.emplace_back("Referrer-Policy", "no-referrer");
    response.extra_headers.emplace_back("X-Frame-Options", "DENY");
    response.extra_headers.emplace_back("Access-Control-Allow-Origin", "*");
    response.extra_headers.emplace_back("Access-Control-Allow-Methods",
                                        "GET, HEAD, POST, OPTIONS");
    response.extra_headers.emplace_back("Access-Control-Allow-Headers", "Content-Type, Range");
    response.extra_headers.emplace_back("Access-Control-Max-Age", "86400");
    response.extra_headers.emplace_back("Access-Control-Expose-Headers",
                                        "ETag, Last-Modified, Content-Range, Allow");
}

inline Response make_status(int status, const std::string& body,
                            const std::string& content_type = "text/plain; charset=utf-8") {
    Response response;
    response.status = status;
    response.status_text = status_text_for(status);
    response.content_type = content_type;
    response.body = body;
    if (!body.empty() && body.back() != '\n' && starts_with(content_type, "text/plain")) {
        response.body.push_back('\n');
    }
    return response;
}

inline Response json_ok(const std::string& body) {
    Response response;
    response.status = 200;
    response.status_text = "OK";
    response.content_type = "application/json; charset=utf-8";
    response.body = body;
    return response;
}

inline Response bad_request(const std::string& message) {
    return make_status(400, message);
}

inline Response not_found() {
    return make_status(404, "404 Not Found");
}

inline Response method_not_allowed(const std::string& allow) {
    Response response = make_status(405, "Method Not Allowed");
    response.extra_headers.emplace_back("Allow", allow);
    return response;
}

inline Response request_timeout() {
    Response response = make_status(408, "Request Timeout");
    response.close = true;
    return response;
}

inline Response payload_too_large() {
    Response response = make_status(413, "Payload Too Large");
    response.close = true;
    return response;
}

inline Response too_many_requests() {
    Response response = make_status(429, "Too Many Requests");
    response.extra_headers.emplace_back("Retry-After", "1");
    return response;
}

inline Response headers_too_large() {
    Response response = make_status(431, "Request Header Fields Too Large");
    response.close = true;
    return response;
}

inline Response internal_error() {
    return make_status(500, "Internal Server Error");
}

inline Response service_unavailable() {
    Response response = make_status(503, "Service Unavailable");
    response.extra_headers.emplace_back("Retry-After", "1");
    response.close = true;
    return response;
}

inline Response cors_preflight() {
    Response response;
    response.status = 204;
    response.status_text = "No Content";
    response.content_type = "text/plain; charset=utf-8";
    return response;
}

inline std::string mime_type(const fs::path& path) {
    const std::string ext = to_lower(path.extension().string());
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".map") return "application/json";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

inline bool send_all(int fd, const char* data, std::size_t size, int timeout_ms = 15000) {
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (offset < size) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return false;
        }
        pollfd item{};
        item.fd = fd;
        item.events = POLLOUT;
        const int ready = ::poll(&item, 1, static_cast<int>(remaining.count()));
        if (ready <= 0) {
            return false;
        }
        if (item.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return false;
        }
        const ssize_t sent = ::send(fd, data + offset, size - offset, 0);
        if (sent < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

inline bool send_all(int fd, const std::string& payload, int timeout_ms = 15000) {
    return send_all(fd, payload.data(), payload.size(), timeout_ms);
}

inline std::string serialize_response(const Request& request, const Response& response,
                                      bool keep_alive) {
    const bool head = request.method == "HEAD";
    const bool no_body = head || response.status == 204 || response.status == 304;
    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << " " << response.status_text << "\r\n";
    out << "Date: " << http_date() << "\r\n";
    out << "Server: AsterForge/" << kVersion << "\r\n";
    const bool event_stream = response.content_type.find("event-stream") != std::string::npos;
    if (!response.content_type.empty() && response.status != 204) {
        out << "Content-Type: " << response.content_type << "\r\n";
    }
    if (event_stream) {
        // SSE is an open stream; a Content-Length of 0 would truncate it.
    } else if (response.status != 204 && response.status != 304) {
        out << "Content-Length: " << response.body.size() << "\r\n";
    } else if (response.status == 304) {
        out << "Content-Length: 0\r\n";
    }
    const bool close = response.close || !keep_alive;
    out << "Connection: " << (close ? "close" : "keep-alive") << "\r\n";
    if (!close) {
        out << "Keep-Alive: timeout=8, max=64\r\n";
    }
    bool has_cache = false;
    bool has_cors = false;
    for (const auto& header : response.extra_headers) {
        if (to_lower(header.first) == "cache-control") {
            has_cache = true;
        }
        if (to_lower(header.first) == "access-control-allow-origin") {
            has_cors = true;
        }
        out << header.first << ": " << header.second << "\r\n";
    }
    if (!has_cache) {
        out << "Cache-Control: no-store\r\n";
    }
    if (!has_cors) {
        out << "X-Content-Type-Options: nosniff\r\n";
        out << "Referrer-Policy: no-referrer\r\n";
        out << "X-Frame-Options: DENY\r\n";
        out << "Access-Control-Allow-Origin: *\r\n";
        out << "Access-Control-Allow-Methods: GET, HEAD, POST, OPTIONS\r\n";
        out << "Access-Control-Allow-Headers: Content-Type, Range\r\n";
        out << "Access-Control-Expose-Headers: ETag, Last-Modified, Content-Range, Allow\r\n";
    }
    out << "\r\n";
    if (!no_body) {
        out << response.body;
    }
    return out.str();
}

enum class ParseStatus { complete, incomplete, bad, headers_too_large, body_too_large };

struct ParseOutcome {
    ParseStatus status = ParseStatus::incomplete;
    Request request;
    std::size_t consumed = 0;
};

inline ParseOutcome try_parse_request(const std::string& buffer, std::size_t max_headers,
                                      std::size_t max_body) {
    ParseOutcome outcome;
    const std::size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (buffer.size() > max_headers) {
            outcome.status = ParseStatus::headers_too_large;
        }
        return outcome;
    }
    if (header_end > max_headers) {
        outcome.status = ParseStatus::headers_too_large;
        return outcome;
    }

    const std::string head = buffer.substr(0, header_end);
    const std::size_t line_end = head.find("\r\n");
    const std::string request_line = line_end == std::string::npos ? head : head.substr(0, line_end);
    std::istringstream line(request_line);
    Request request;
    line >> request.method >> request.target >> request.version;
    if (request.method.empty() || request.target.empty() || request.version.empty()) {
        outcome.status = ParseStatus::bad;
        return outcome;
    }
    if (request.target.size() > 8192) {
        outcome.status = ParseStatus::bad;
        return outcome;
    }

    const std::size_t query_start = request.target.find('?');
    request.path = url_decode(request.target.substr(0, query_start));
    if (query_start != std::string::npos) {
        request.query = parse_query(request.target.substr(query_start + 1));
    }
    if (request.path.empty()) {
        request.path = "/";
    }

    std::size_t cursor = line_end == std::string::npos ? head.size() : line_end + 2;
    while (cursor < head.size()) {
        const std::size_t next = head.find("\r\n", cursor);
        const std::string raw =
            head.substr(cursor, next == std::string::npos ? std::string::npos : next - cursor);
        if (raw.empty()) {
            break;
        }
        const std::size_t colon = raw.find(':');
        if (colon == std::string::npos) {
            outcome.status = ParseStatus::bad;
            return outcome;
        }
        const std::string key = to_lower(trim(raw.substr(0, colon)));
        const std::string value = trim(raw.substr(colon + 1));
        request.headers[key] = value;
        if (next == std::string::npos) {
            break;
        }
        cursor = next + 2;
        if (request.headers.size() > 100) {
            outcome.status = ParseStatus::headers_too_large;
            return outcome;
        }
    }

    std::size_t content_length = 0;
    const std::string length_header = header_get(request, "content-length");
    if (!length_header.empty()) {
        try {
            if (length_header.find('-') != std::string::npos) {
                outcome.status = ParseStatus::bad;
                return outcome;
            }
            const unsigned long long parsed = std::stoull(length_header);
            if (parsed > max_body) {
                outcome.status = ParseStatus::body_too_large;
                outcome.request = request;
                return outcome;
            }
            content_length = static_cast<std::size_t>(parsed);
        } catch (...) {
            outcome.status = ParseStatus::bad;
            return outcome;
        }
    }

    const std::size_t total = header_end + 4 + content_length;
    if (buffer.size() < total) {
        outcome.status = ParseStatus::incomplete;
        outcome.request = request;
        return outcome;
    }
    request.body = buffer.substr(header_end + 4, content_length);
    outcome.status = ParseStatus::complete;
    outcome.request = std::move(request);
    outcome.consumed = total;
    return outcome;
}

}  // namespace aster
