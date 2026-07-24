#pragma once

#include "util.hpp"

#include <map>
#include <sstream>
#include <string>

#include <sys/socket.h>
#include <unistd.h>

namespace aster {

inline constexpr const char* kVersion = "1.2.0";

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
};

inline bool send_all(int socket_fd, const std::string& payload) {
    const char* data = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const ssize_t sent = ::send(socket_fd, data, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

inline std::size_t content_length_from_headers(const std::map<std::string, std::string>& headers) {
    const auto it = headers.find("content-length");
    if (it == headers.end()) {
        return 0;
    }
    try {
        const long value = std::stol(it->second);
        if (value < 0) {
            return 0;
        }
        // Cap body size for demos / safety (1 MiB).
        return static_cast<std::size_t>(std::min<long>(value, 1024 * 1024));
    } catch (...) {
        return 0;
    }
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

// Read full HTTP request including body based on Content-Length.
inline bool read_http_request(int client_fd, Request& request) {
    std::string raw;
    char buffer[4096];

    while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 65536) {
        const ssize_t received = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return false;
        }
        raw.append(buffer, static_cast<std::size_t>(received));
    }

    if (raw.find("\r\n\r\n") == std::string::npos) {
        return false;
    }

    request = parse_request_headers(raw);
    const std::size_t expected = content_length_from_headers(request.headers);

    while (request.body.size() < expected && request.body.size() < 1024 * 1024) {
        const ssize_t received = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }
        request.body.append(buffer, static_cast<std::size_t>(received));
    }

    if (request.body.size() > expected) {
        request.body.resize(expected);
    }
    return true;
}

inline Response json_response(const std::string& body, int status = 200, const std::string& status_text = "OK") {
    return Response{status, status_text, "application/json; charset=utf-8", body, true};
}

inline Response not_found() {
    return Response{404, "Not Found", "text/plain; charset=utf-8", "404 Not Found\n", true};
}

inline Response bad_request(const std::string& message) {
    return Response{400, "Bad Request", "text/plain; charset=utf-8", message + "\n", true};
}

inline Response method_not_allowed(const std::string& allow) {
    Response response{405, "Method Not Allowed", "text/plain; charset=utf-8", "405 Method Not Allowed\n", true};
    // Allow header is injected in send_response via content_type abuse? Better add optional headers.
    // We'll put Allow in the body note for simplicity and set via specialized send if needed.
    (void)allow;
    return response;
}

inline std::string status_text_for(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

inline bool send_response(int client_fd, Response response, const std::string& allow_methods = "") {
    if (!response.include_body) {
        // HEAD: keep Content-Length of the would-be body but omit payload.
    }

    const std::size_t body_size = response.body.size();
    std::ostringstream payload;
    payload << "HTTP/1.1 " << response.status << " " << response.status_text << "\r\n";
    payload << "Content-Type: " << response.content_type << "\r\n";
    payload << "Content-Length: " << body_size << "\r\n";
    payload << "Cache-Control: no-store\r\n";
    payload << "Connection: close\r\n";
    payload << "X-Content-Type-Options: nosniff\r\n";
    payload << "Access-Control-Allow-Origin: *\r\n";
    payload << "Access-Control-Allow-Methods: GET, HEAD, POST, OPTIONS\r\n";
    payload << "Access-Control-Allow-Headers: Content-Type\r\n";
    if (!allow_methods.empty()) {
        payload << "Allow: " << allow_methods << "\r\n";
    }
    payload << "\r\n";
    if (response.include_body) {
        payload << response.body;
    }
    return send_all(client_fd, payload.str());
}

}  // namespace aster
