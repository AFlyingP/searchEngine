#include "server/http_server.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "rank/snippet.hpp"

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif
using socket_t = SOCKET;
using recv_len_t = int;
using send_len_t = int;
using sock_ssize_t = int;
constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
constexpr int SOCK_ERR = SOCKET_ERROR;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
using recv_len_t = size_t;
using send_len_t = size_t;
using sock_ssize_t = ssize_t;
constexpr socket_t INVALID_SOCK = -1;
constexpr int SOCK_ERR = -1;
#endif

namespace needlefish {

namespace {

std::string url_decode(std::string_view src) {
    std::string dst;
    dst.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()) {
            int val = 0;
            std::istringstream is(std::string(src.substr(i + 1, 2)));
            if (is >> std::hex >> val) {
                dst.push_back(static_cast<char>(val));
                i += 2;
            } else {
                dst.push_back(src[i]);
            }
        } else if (src[i] == '+') {
            dst.push_back(' ');
        } else {
            dst.push_back(src[i]);
        }
    }
    return dst;
}

std::string json_escape(std::string_view str) {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string get_mime_type(std::string_view path) {
    if (path.ends_with(".html") || path.ends_with(".htm"))
        return "text/html; charset=utf-8";
    if (path.ends_with(".css"))
        return "text/css; charset=utf-8";
    if (path.ends_with(".js"))
        return "application/javascript; charset=utf-8";
    if (path.ends_with(".json"))
        return "application/json; charset=utf-8";
    if (path.ends_with(".svg"))
        return "image/svg+xml";
    if (path.ends_with(".png"))
        return "image/png";
    if (path.ends_with(".ico"))
        return "image/x-icon";
    return "application/octet-stream";
}

}  // namespace

std::string HttpResponse::to_http_string() const {
    std::string res;
    res.reserve(256 + body.size());

    res += "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n";
    res += "Content-Type: " + content_type + "\r\n";
    res += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    res += "Access-Control-Allow-Origin: *\r\n";
    res += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    res += "Access-Control-Allow-Headers: Content-Type\r\n";
    res += "Connection: close\r\n";

    for (const auto& [k, v] : headers) {
        res += k + ": " + v + "\r\n";
    }

    res += "\r\n";
    res += body;
    return res;
}

HttpServer::HttpServer(IndexView& index, std::string host, uint16_t port)
    : index_(index), engine_(index), host_(std::move(host)), port_(port) {
#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
}

HttpServer::~HttpServer() {
    stop();
#if defined(_WIN32) || defined(_WIN64)
    WSACleanup();
#endif
}

HttpServer::HttpServer(HttpServer&& other) noexcept
    : index_(other.index_)
    , engine_(std::move(other.engine_))
    , host_(std::move(other.host_))
    , port_(other.port_)
    , static_dir_(std::move(other.static_dir_))
    , is_running_(other.is_running_)
    , server_socket_(other.server_socket_) {
    other.server_socket_ = static_cast<uintptr_t>(INVALID_SOCK);
    other.is_running_ = false;
}

HttpServer& HttpServer::operator=(HttpServer&& other) noexcept {
    if (this != &other) {
        stop();
        host_ = std::move(other.host_);
        port_ = other.port_;
        static_dir_ = std::move(other.static_dir_);
        is_running_ = other.is_running_;
        server_socket_ = other.server_socket_;
        other.server_socket_ = static_cast<uintptr_t>(INVALID_SOCK);
        other.is_running_ = false;
    }
    return *this;
}

HttpRequest HttpServer::parse_request(std::string_view raw_request) {
    HttpRequest req;
    if (raw_request.empty()) {
        return req;
    }

    size_t line_end = raw_request.find("\r\n");
    if (line_end == std::string_view::npos) {
        line_end = raw_request.find('\n');
    }
    if (line_end == std::string_view::npos) {
        return req;
    }

    std::string_view req_line = raw_request.substr(0, line_end);
    size_t method_end = req_line.find(' ');
    if (method_end == std::string_view::npos) {
        return req;
    }
    req.method = std::string(req_line.substr(0, method_end));

    size_t target_start = method_end + 1;
    size_t target_end = req_line.find(' ', target_start);
    if (target_end == std::string_view::npos) {
        target_end = req_line.size();
    }
    std::string_view full_target = req_line.substr(target_start, target_end - target_start);

    size_t q_pos = full_target.find('?');
    if (q_pos != std::string_view::npos) {
        req.path = url_decode(full_target.substr(0, q_pos));
        req.raw_query = std::string(full_target.substr(q_pos + 1));

        std::string_view q_rem = req.raw_query;
        while (!q_rem.empty()) {
            size_t amp = q_rem.find('&');
            std::string_view pair = (amp == std::string_view::npos) ? q_rem : q_rem.substr(0, amp);
            size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                std::string k = url_decode(pair.substr(0, eq));
                std::string v = url_decode(pair.substr(eq + 1));
                req.query_params[k] = v;
            } else {
                req.query_params[url_decode(pair)] = "";
            }
            if (amp == std::string_view::npos)
                break;
            q_rem = q_rem.substr(amp + 1);
        }
    } else {
        req.path = url_decode(full_target);
    }

    size_t header_start = line_end + (raw_request[line_end] == '\r' ? 2 : 1);
    size_t body_sep = raw_request.find("\r\n\r\n", header_start);
    if (body_sep != std::string_view::npos) {
        req.body = std::string(raw_request.substr(body_sep + 4));
    }

    return req;
}

HttpResponse HttpServer::handle_api_search(const HttpRequest& req) const {
    auto q_it = req.query_params.find("q");
    if (q_it == req.query_params.end() || q_it->second.empty()) {
        return HttpResponse{.status_code = 400,
                            .status_text = "Bad Request",
                            .body = "{\"error\": \"Missing required 'q' query parameter\"}"};
    }

    const std::string& query_str = q_it->second;
    size_t limit = 10;
    auto limit_it = req.query_params.find("limit");
    if (limit_it != req.query_params.end()) {
        try {
            limit = std::stoul(limit_it->second);
            if (limit == 0)
                limit = 10;
            if (limit > 100)
                limit = 100;
        } catch (...) {
            limit = 10;
        }
    }

    std::string mode = "auto";
    auto mode_it = req.query_params.find("mode");
    if (mode_it != req.query_params.end()) {
        mode = mode_it->second;
    }

    HybridSearchResult result;
    if (mode == "fuzzy") {
        result = engine_.search_fuzzy(query_str, 2, limit);
    } else if (mode == "regex") {
        result = engine_.search_regex(query_str, limit);
    } else if (mode == "substring") {
        result = engine_.search_substring(query_str, limit);
    } else {
        result = engine_.search(query_str, limit);
    }

    std::string mode_str = "standard";
    switch (result.query_type) {
        case HybridQueryType::Standard:
            mode_str = "standard";
            break;
        case HybridQueryType::Fuzzy:
            mode_str = "fuzzy";
            break;
        case HybridQueryType::Regex:
            mode_str = "regex";
            break;
        case HybridQueryType::Substring:
            mode_str = "substring";
            break;
    }

    std::ostringstream json;
    json << "{\n";
    json << "  \"query\": \"" << json_escape(query_str) << "\",\n";
    json << "  \"mode\": \"" << mode_str << "\",\n";
    json << "  \"took_us\": " << result.took_us << ",\n";
    json << "  \"took_ms\": " << (static_cast<double>(result.took_us) / 1000.0) << ",\n";
    json << "  \"total_hits\": " << result.hits.size() << ",\n";
    json << "  \"did_you_mean\": \"" << json_escape(result.correction_suggestion) << "\",\n";
    json << "  \"hits\": [\n";

    Analyzer analyzer;
    auto query_tokens = analyzer.analyze(query_str);
    std::vector<std::string> qterms;
    for (const auto& tok : query_tokens) {
        qterms.push_back(tok.term);
    }
    SnippetGenerator snippet_gen;

    for (size_t i = 0; i < result.hits.size(); ++i) {
        const auto& hit = result.hits[i];
        std::string_view title = index_.doc_title(hit.doc_id);
        std::string_view text = index_.doc_text(hit.doc_id);
        std::string snippet = snippet_gen.highlight(text, qterms);

        json << "    {\n";
        json << "      \"rank\": " << (i + 1) << ",\n";
        json << "      \"doc_id\": " << hit.doc_id << ",\n";
        json << "      \"score\": " << hit.score << ",\n";
        json << "      \"title\": \"" << json_escape(title) << "\",\n";
        json << "      \"snippet\": \"" << json_escape(snippet) << "\"\n";
        json << "    }" << (i + 1 < result.hits.size() ? "," : "") << "\n";
    }

    json << "  ]\n";
    json << "}\n";

    return HttpResponse{.status_code = 200, .status_text = "OK", .body = json.str()};
}

HttpResponse HttpServer::handle_api_suggest(const HttpRequest& req) const {
    auto q_it = req.query_params.find("q");
    if (q_it == req.query_params.end() || q_it->second.empty()) {
        return HttpResponse{.status_code = 400,
                            .status_text = "Bad Request",
                            .body = "{\"error\": \"Missing required 'q' query parameter\"}"};
    }

    const std::string& prefix = q_it->second;
    bool fuzzy = false;
    auto fuzzy_it = req.query_params.find("fuzzy");
    if (fuzzy_it != req.query_params.end() &&
        (fuzzy_it->second == "true" || fuzzy_it->second == "1")) {
        fuzzy = true;
    }

    std::vector<Suggestion> suggestions;
    if (fuzzy) {
        suggestions = engine_.autocomplete().fuzzy_suggest(prefix, 2, 8);
    } else {
        suggestions = engine_.autocomplete().prefix_suggest(prefix, 8);
    }

    std::ostringstream json;
    json << "{\n";
    json << "  \"query\": \"" << json_escape(prefix) << "\",\n";
    json << "  \"suggestions\": [\n";

    for (size_t i = 0; i < suggestions.size(); ++i) {
        const auto& s = suggestions[i];
        json << "    {\n";
        json << "      \"term\": \"" << json_escape(s.text) << "\",\n";
        json << "      \"doc_freq\": " << s.doc_freq << ",\n";
        json << "      \"edit_distance\": " << s.edit_distance << "\n";
        json << "    }" << (i + 1 < suggestions.size() ? "," : "") << "\n";
    }

    json << "  ]\n";
    json << "}\n";

    return HttpResponse{.status_code = 200, .status_text = "OK", .body = json.str()};
}

HttpResponse HttpServer::handle_api_stats(const HttpRequest&) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"total_docs\": " << index_.total_docs() << ",\n";
    json << "  \"total_tokens\": " << index_.stats().total_tokens << ",\n";
    json << "  \"avg_doc_length\": " << index_.stats().avg_doc_len << ",\n";
    json << "  \"unique_terms\": " << index_.term_dict().num_terms() << ",\n";
    json << "  \"trie_nodes\": " << index_.term_dict().num_nodes() << ",\n";
    json << "  \"has_fm_index\": " << (index_.has_fm_index() ? "true" : "false") << ",\n";
    json << "  \"file_size_bytes\": " << index_.file_size() << ",\n";
    json << "  \"bm25\": {\"k1\": 0.9, \"b\": 0.4}\n";
    json << "}\n";

    return HttpResponse{.status_code = 200, .status_text = "OK", .body = json.str()};
}

HttpResponse HttpServer::handle_static_file(std::string_view path) const {
    if (path == "/" || path.empty()) {
        path = "/index.html";
    }

    // Reject path traversal attempts explicitly
    if (path.find("..") != std::string_view::npos || path.find('\\') != std::string_view::npos) {
        return HttpResponse{.status_code = 403,
                            .status_text = "Forbidden",
                            .content_type = "text/plain",
                            .body = "403 Forbidden: Path traversal prohibited"};
    }

    std::error_code ec;
    std::filesystem::path base_dir = std::filesystem::weakly_canonical(std::filesystem::path(static_dir_), ec);
    if (ec) {
        base_dir = std::filesystem::absolute(std::filesystem::path(static_dir_));
    }

    std::filesystem::path relative_subpath = path.substr(path.starts_with('/') ? 1 : 0);
    std::filesystem::path candidate_path = std::filesystem::weakly_canonical(base_dir / relative_subpath, ec);
    if (ec) {
        return HttpResponse{.status_code = 400,
                            .status_text = "Bad Request",
                            .content_type = "text/plain",
                            .body = "400 Bad Request"};
    }

    // Verify canonical path is strictly contained within base_dir
    auto base_str = base_dir.string();
    auto cand_str = candidate_path.string();
    if (!cand_str.starts_with(base_str)) {
        return HttpResponse{.status_code = 403,
                            .status_text = "Forbidden",
                            .content_type = "text/plain",
                            .body = "403 Forbidden: Access denied"};
    }

    if (!std::filesystem::exists(candidate_path) || std::filesystem::is_directory(candidate_path)) {
        return HttpResponse{.status_code = 404,
                            .status_text = "Not Found",
                            .content_type = "text/plain",
                            .body = "404 Not Found"};
    }

    std::ifstream file(candidate_path, std::ios::binary);
    if (!file) {
        return HttpResponse{.status_code = 500,
                            .status_text = "Internal Server Error",
                            .content_type = "text/plain",
                            .body = "500 Failed to read file"};
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return HttpResponse{.status_code = 200,
                        .status_text = "OK",
                        .content_type = get_mime_type(candidate_path.string()),
                        .body = std::move(content)};
}

HttpResponse HttpServer::handle_request(const HttpRequest& req) const {
    if (req.method == "OPTIONS") {
        return HttpResponse{.status_code = 204, .status_text = "No Content", .body = ""};
    }

    if (req.path == "/api/search") {
        return handle_api_search(req);
    }
    if (req.path == "/api/suggest") {
        return handle_api_suggest(req);
    }
    if (req.path == "/api/stats") {
        return handle_api_stats(req);
    }

    return handle_static_file(req.path);
}

void HttpServer::start() {
    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd == INVALID_SOCK) {
        std::cerr << "Failed to create socket." << std::endl;
        return;
    }

    int opt = 1;
#if defined(_WIN32) || defined(_WIN64)
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt),
               sizeof(opt));
#else
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    if (host_ == "0.0.0.0" || host_ == "*" || host_.empty()) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr);
    }

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) ==
        SOCK_ERR) {
        std::cerr << "Failed to bind to " << host_ << ":" << port_ << std::endl;
#if defined(_WIN32) || defined(_WIN64)
        closesocket(listen_fd);
#else
        close(listen_fd);
#endif
        return;
    }

    if (listen(listen_fd, 64) == SOCK_ERR) {
        std::cerr << "Failed to listen on socket." << std::endl;
#if defined(_WIN32) || defined(_WIN64)
        closesocket(listen_fd);
#else
        close(listen_fd);
#endif
        return;
    }

    server_socket_ = static_cast<uintptr_t>(listen_fd);
    is_running_ = true;

    std::cout << "Needlefish search server running on http://" << host_ << ":" << port_
              << std::endl;
    std::cout << "Serving static assets from ./" << static_dir_ << "/" << std::endl;

    while (is_running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        socket_t client_fd =
            accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd == INVALID_SOCK) {
            if (!is_running_)
                break;
            continue;
        }

#if defined(_WIN32) || defined(_WIN64)
        DWORD timeout_ms = 3000;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms),
                   sizeof(timeout_ms));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms),
                   sizeof(timeout_ms));
#else
        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        std::array<char, 4096> buffer{};
        sock_ssize_t bytes_read =
            recv(client_fd, buffer.data(), static_cast<recv_len_t>(buffer.size() - 1), 0);
        if (bytes_read > 0) {
            buffer[static_cast<size_t>(bytes_read)] = '\0';
            HttpRequest req =
                parse_request(std::string_view(buffer.data(), static_cast<size_t>(bytes_read)));
            HttpResponse resp = handle_request(req);
            std::string raw_resp = resp.to_http_string();
            size_t total_sent = 0;
            while (total_sent < raw_resp.size()) {
                sock_ssize_t sent = send(client_fd, raw_resp.data() + total_sent,
                                         static_cast<send_len_t>(raw_resp.size() - total_sent), 0);
                if (sent <= 0)
                    break;
                total_sent += static_cast<size_t>(sent);
            }
        }

#if defined(_WIN32) || defined(_WIN64)
        shutdown(client_fd, SD_BOTH);
        closesocket(client_fd);
#else
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
#endif
    }
}

void HttpServer::stop() {
    if (server_socket_ != static_cast<uintptr_t>(INVALID_SOCK)) {
        is_running_ = false;
#if defined(_WIN32) || defined(_WIN64)
        closesocket(static_cast<socket_t>(server_socket_));
#else
        close(static_cast<socket_t>(server_socket_));
#endif
        server_socket_ = static_cast<uintptr_t>(INVALID_SOCK);
    }
}

}  // namespace needlefish
