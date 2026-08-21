#include "server/http_server.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

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

int hex_val(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string url_decode(std::string_view src, bool is_query = false) {
    std::string dst;
    dst.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()) {
            int h1 = hex_val(src[i + 1]);
            int h2 = hex_val(src[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                dst.push_back(static_cast<char>((h1 << 4) | h2));
                i += 2;
                continue;
            }
        }
        if (src[i] == '+' && is_query) {
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

#include <zlib.h>

// Token bucket rate limiter with LRU eviction per client IP (IPv4 and IPv6)
struct RateLimiter {
    struct Bucket {
        double tokens{20.0};
        std::chrono::steady_clock::time_point last_update{std::chrono::steady_clock::now()};
    };
    std::mutex mtx;
    static constexpr size_t MAX_IPS = 10000;
    std::list<std::string> lru_list;
    std::unordered_map<std::string, std::pair<Bucket, std::list<std::string>::iterator>> buckets;

    bool allow(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mtx);
        auto now = std::chrono::steady_clock::now();
        auto it = buckets.find(ip);
        if (it == buckets.end()) {
            if (buckets.size() >= MAX_IPS) {
                std::string oldest = lru_list.back();
                lru_list.pop_back();
                buckets.erase(oldest);
            }
            lru_list.push_front(ip);
            buckets[ip] = {Bucket{.tokens = 19.0, .last_update = now}, lru_list.begin()};
            return true;
        }

        lru_list.splice(lru_list.begin(), lru_list, it->second.second);
        auto& b = it->second.first;
        double elapsed = std::chrono::duration<double>(now - b.last_update).count();
        b.last_update = now;
        b.tokens = std::min(20.0, b.tokens + elapsed * 10.0);  // 10 QPS fill rate
        if (b.tokens >= 1.0) {
            b.tokens -= 1.0;
            return true;
        }
        return false;
    }
};

RateLimiter g_rate_limiter;

std::string gzip_compress(std::string_view data) {
    if (data.empty()) return {};
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return std::string(data);
    }
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());

    std::string out;
    out.resize(deflateBound(&zs, static_cast<uLong>(data.size())) + 32);
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());

    int ret = deflate(&zs, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&zs);
        return std::string(data);
    }
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return out;
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
    , is_running_(other.is_running_.load())
    , server_socket_(other.server_socket_) {
    other.server_socket_ = static_cast<uintptr_t>(INVALID_SOCK);
    other.is_running_.store(false);
}

HttpServer& HttpServer::operator=(HttpServer&& other) noexcept {
    if (this != &other) {
        stop();
        host_ = std::move(other.host_);
        port_ = other.port_;
        static_dir_ = std::move(other.static_dir_);
        is_running_.store(other.is_running_.load());
        server_socket_ = other.server_socket_;
        other.server_socket_ = static_cast<uintptr_t>(INVALID_SOCK);
        other.is_running_.store(false);
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
        req.path = url_decode(full_target.substr(0, q_pos), false);
        req.raw_query = std::string(full_target.substr(q_pos + 1));

        std::string_view q_rem = req.raw_query;
        while (!q_rem.empty()) {
            size_t amp = q_rem.find('&');
            std::string_view pair = (amp == std::string_view::npos) ? q_rem : q_rem.substr(0, amp);
            size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                std::string k = url_decode(pair.substr(0, eq), true);
                std::string v = url_decode(pair.substr(eq + 1), true);
                req.query_params[k] = v;
            } else {
                req.query_params[url_decode(pair, true)] = "";
            }
            if (amp == std::string_view::npos)
                break;
            q_rem = q_rem.substr(amp + 1);
        }
    } else {
        req.path = url_decode(full_target, false);
    }

    // Parse headers
    size_t header_start = line_end + (raw_request[line_end] == '\r' ? 2 : 1);
    size_t body_sep = raw_request.find("\r\n\r\n", header_start);
    size_t headers_end = (body_sep != std::string_view::npos) ? body_sep : raw_request.size();

    std::string_view headers_block = raw_request.substr(header_start, headers_end - header_start);
    while (!headers_block.empty()) {
        size_t next_line = headers_block.find("\r\n");
        std::string_view hline = (next_line != std::string_view::npos) ? headers_block.substr(0, next_line) : headers_block;
        size_t colon = hline.find(':');
        if (colon != std::string_view::npos) {
            std::string hname = std::string(hline.substr(0, colon));
            std::transform(hname.begin(), hname.end(), hname.begin(), [](unsigned char c) { return std::tolower(c); });
            size_t val_start = hline.find_first_not_of(" \t", colon + 1);
            std::string hval = (val_start != std::string_view::npos) ? std::string(hline.substr(val_start)) : "";
            req.headers[hname] = hval;
        }
        if (next_line == std::string_view::npos) break;
        headers_block = headers_block.substr(next_line + 2);
    }

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
    if (query_str.size() > 256) {
        return HttpResponse{.status_code = 400,
                            .status_text = "Bad Request",
                            .body = "{\"error\": \"Query length exceeds maximum limit of 256 characters\"}"};
    }

    size_t limit = 10;
    auto k_it = req.query_params.find("k");
    if (k_it == req.query_params.end()) {
        k_it = req.query_params.find("limit");
    }
    if (k_it != req.query_params.end()) {
        try {
            limit = std::stoul(k_it->second);
            if (limit == 0) limit = 10;
            if (limit > 100) limit = 100;
        } catch (...) {
            limit = 10;
        }
    }

    std::string mode = "auto";
    auto mode_it = req.query_params.find("mode");
    if (mode_it != req.query_params.end()) {
        mode = mode_it->second;
    }

    size_t fuzzy_dist = 0;
    auto fuzzy_it = req.query_params.find("fuzzy");
    if (fuzzy_it != req.query_params.end()) {
        try {
            fuzzy_dist = std::min<size_t>(2, std::stoul(fuzzy_it->second));
        } catch (...) {
            fuzzy_dist = 0;
        }
    }

    HybridSearchResult result;
    if (fuzzy_dist > 0 || mode == "fuzzy") {
        result = engine_.search_fuzzy(query_str, fuzzy_dist > 0 ? fuzzy_dist : 2, limit);
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
    json << "  \"total_estimate\": " << result.hits.size() << ",\n";
    json << "  \"total_hits\": " << result.hits.size() << ",\n";
    json << "  \"corrected\": \"" << json_escape(result.correction_suggestion) << "\",\n";
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
        uint32_t ext_id = index_.external_id(hit.doc_id);
        float safe_score = (std::isnan(hit.score) || std::isinf(hit.score)) ? 0.0f : hit.score;

        json << "    {\n";
        json << "      \"rank\": " << (i + 1) << ",\n";
        json << "      \"id\": " << ext_id << ",\n";
        json << "      \"doc_id\": " << ext_id << ",\n";
        json << "      \"score\": " << safe_score << ",\n";
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
    if (q_it == req.query_params.end()) {
        q_it = req.query_params.find("prefix");
    }
    if (q_it == req.query_params.end() || q_it->second.empty()) {
        return HttpResponse{.status_code = 400,
                            .status_text = "Bad Request",
                            .body = "{\"error\": \"Missing required 'q' query parameter\"}"};
    }

    const std::string& prefix = q_it->second;
    if (prefix.size() > 256) {
        return HttpResponse{.status_code = 400,
                            .status_text = "Bad Request",
                            .body = "{\"error\": \"Query length exceeds maximum limit of 256 characters\"}"};
    }

    bool fuzzy = false;
    auto fuzzy_it = req.query_params.find("fuzzy");
    if (fuzzy_it != req.query_params.end() &&
        (fuzzy_it->second == "true" || fuzzy_it->second == "1")) {
        fuzzy = true;
    }

    const auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<Suggestion> suggestions;
    if (fuzzy) {
        suggestions = engine_.autocomplete().fuzzy_suggest(prefix, 2, 8);
    } else {
        suggestions = engine_.autocomplete().prefix_suggest(prefix, 8);
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const uint64_t took_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    std::ostringstream json;
    json << "{\n";
    json << "  \"query\": \"" << json_escape(prefix) << "\",\n";
    json << "  \"took_us\": " << took_us << ",\n";
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
    const auto& stats = index_.stats();
    std::ostringstream json;
    json << "{\n";
    json << "  \"total_docs\": " << index_.total_docs() << ",\n";
    json << "  \"total_tokens\": " << stats.total_tokens << ",\n";
    json << "  \"avg_doc_length\": " << stats.avg_doc_len << ",\n";
    json << "  \"unique_terms\": " << index_.term_dict().num_terms() << ",\n";
    json << "  \"trie_nodes\": " << index_.term_dict().num_nodes() << ",\n";
    json << "  \"has_fm_index\": " << (index_.has_fm_index() ? "true" : "false") << ",\n";
    json << "  \"file_size_bytes\": " << index_.file_size() << ",\n";
    json << "  \"bm25\": {\"k1\": " << stats.k1 << ", \"b\": " << stats.b << "}\n";
    json << "}\n";

    return HttpResponse{.status_code = 200, .status_text = "OK", .body = json.str()};
}

HttpResponse HttpServer::handle_api_health(const HttpRequest&) const {
    auto now = std::chrono::steady_clock::now();
    uint64_t uptime_sec = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count());

    std::ostringstream json;
    json << "{\n";
    json << "  \"status\": \"healthy\",\n";
    json << "  \"version\": \"1.0.0\",\n";
    json << "  \"index_checksum\": " << index_.checksum() << ",\n";
    json << "  \"total_docs\": " << index_.total_docs() << ",\n";
    json << "  \"uptime_seconds\": " << uptime_sec << "\n";
    json << "}\n";

    return HttpResponse{.status_code = 200, .status_text = "OK", .body = json.str()};
}

HttpResponse HttpServer::handle_static_file(std::string_view path) const {
    if (path == "/" || path.empty()) {
        path = "/index.html";
    }

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
    std::string mime = get_mime_type(candidate_path.string());
    return HttpResponse{.status_code = 200,
                        .status_text = "OK",
                        .content_type = std::move(mime),
                        .body = std::move(content)};
}

void HttpServer::worker_loop() {
    while (!stop_workers_.load()) {
        uintptr_t sock_val = 0;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !task_queue_.empty() || stop_workers_.load();
            });
            if (stop_workers_.load() && task_queue_.empty()) {
                break;
            }
            if (!task_queue_.empty()) {
                sock_val = task_queue_.front();
                task_queue_.pop();
            }
        }
        if (sock_val == 0) continue;
        socket_t client_fd = static_cast<socket_t>(sock_val);

        // Worker exclusively owns client_fd end-to-end
#if defined(_WIN32) || defined(_WIN64)
        DWORD timeout_ms = 5000;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms),
                   sizeof(timeout_ms));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms),
                   sizeof(timeout_ms));
#else
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        auto start_recv_time = std::chrono::steady_clock::now();
        std::string raw_request;
        raw_request.reserve(4096);
        std::array<char, 2048> chunk{};
        size_t expected_total_size = 0;
        bool headers_complete = false;
        bool timed_out = false;

        while (raw_request.size() < 65536) {
            auto now = std::chrono::steady_clock::now();
            double total_elapsed = std::chrono::duration<double>(now - start_recv_time).count();
            if (total_elapsed > 15.0) {
                timed_out = true;
                break;
            }

            sock_ssize_t bytes_read =
                recv(client_fd, chunk.data(), static_cast<recv_len_t>(chunk.size()), 0);
            if (bytes_read <= 0) {
                break;
            }
            raw_request.append(chunk.data(), static_cast<size_t>(bytes_read));

            if (!headers_complete) {
                size_t sep = raw_request.find("\r\n\r\n");
                if (sep != std::string::npos) {
                    headers_complete = true;
                    size_t body_start = sep + 4;
                    // Check for Content-Length
                    size_t cl_pos = raw_request.find("Content-Length:");
                    if (cl_pos == std::string::npos) {
                        cl_pos = raw_request.find("content-length:");
                    }
                    if (cl_pos != std::string::npos && cl_pos < sep) {
                        size_t val_s = raw_request.find_first_not_of(" \t", cl_pos + 15);
                        size_t val_e = raw_request.find("\r\n", val_s);
                        try {
                            size_t cl = std::stoul(raw_request.substr(val_s, val_e - val_s));
                            expected_total_size = body_start + cl;
                        } catch (...) {
                            expected_total_size = body_start;
                        }
                    } else {
                        expected_total_size = body_start;
                    }
                }
            }

            if (headers_complete && raw_request.size() >= expected_total_size) {
                break;
            }
        }

        if (timed_out) {
            HttpResponse resp{.status_code = 408,
                              .status_text = "Request Timeout",
                              .body = "{\"error\": \"Request framing timeout (15s limit)\"}"};
            std::string raw_resp = resp.to_http_string();
            send(client_fd, raw_resp.data(), static_cast<send_len_t>(raw_resp.size()), 0);
        } else if (!raw_request.empty()) {
            HttpRequest req = parse_request(raw_request);
            HttpResponse resp = handle_request(req);

            // Gzip compression check
            bool client_accepts_gzip = false;
            auto it = req.headers.find("accept-encoding");
            if (it == req.headers.end()) {
                it = req.headers.find("Accept-Encoding");
            }
            if (it != req.headers.end() && it->second.find("gzip") != std::string::npos) {
                client_accepts_gzip = true;
            }

            if (client_accepts_gzip && req.method != "HEAD" && resp.body.size() > 1024) {
                std::string compressed = gzip_compress(resp.body);
                if (compressed.size() < resp.body.size()) {
                    resp.body = std::move(compressed);
                    resp.headers.push_back({"Content-Encoding", "gzip"});
                    resp.headers.push_back({"Vary", "Accept-Encoding"});
                }
            }

            std::string raw_resp = resp.to_http_string();
            size_t total_sent = 0;
            while (total_sent < raw_resp.size()) {
                sock_ssize_t sent = send(client_fd, raw_resp.data() + total_sent,
                                         static_cast<send_len_t>(raw_resp.size() - total_sent), 0);
                if (sent <= 0) break;
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

HttpResponse HttpServer::handle_request(const HttpRequest& req) const {
    try {
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
        if (req.path == "/api/health") {
            return handle_api_health(req);
        }

        return handle_static_file(req.path);
    } catch (const std::exception& e) {
        std::ostringstream json;
        json << "{\"error\": \"" << json_escape(e.what()) << "\"}";
        return HttpResponse{.status_code = 400,
                            .status_text = "Bad Request",
                            .body = json.str()};
    } catch (...) {
        return HttpResponse{.status_code = 500,
                            .status_text = "Internal Server Error",
                            .body = "{\"error\": \"Internal server error\"}"};
    }
}

void HttpServer::start() {
    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd == INVALID_SOCK) {
        std::cerr << "Failed to create socket." << std::endl;
        return;
    }

    int opt = 1;
#if defined(_WIN32) || defined(_WIN64)
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    if (host_ == "0.0.0.0" || host_ == "*" || host_.empty()) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid host address: " << host_ << ", falling back to 0.0.0.0" << std::endl;
            server_addr.sin_addr.s_addr = INADDR_ANY;
        }
    }

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCK_ERR) {
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
    is_running_.store(true);
    stop_workers_.store(false);
    start_time_ = std::chrono::steady_clock::now();

    // Spawn worker threads
    workers_.clear();
    for (size_t i = 0; i < NUM_WORKERS; ++i) {
        workers_.emplace_back(&HttpServer::worker_loop, this);
    }

    std::cout << "Needlefish search server running on http://" << host_ << ":" << port_ << std::endl;
    std::cout << "Serving static assets from ./" << static_dir_ << "/" << std::endl;

    while (is_running_.load()) {
        sockaddr_storage client_storage{};
        socklen_t client_len = sizeof(client_storage);
        socket_t client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_storage), &client_len);
        if (client_fd == INVALID_SOCK) {
            if (!is_running_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Format client IP for both IPv4 and IPv6
        char client_ip[INET6_ADDRSTRLEN] = "127.0.0.1";
        if (client_storage.ss_family == AF_INET6) {
            auto* s6 = reinterpret_cast<sockaddr_in6*>(&client_storage);
            inet_ntop(AF_INET6, &s6->sin6_addr, client_ip, sizeof(client_ip));
        } else {
            auto* s4 = reinterpret_cast<sockaddr_in*>(&client_storage);
            inet_ntop(AF_INET, &s4->sin_addr, client_ip, sizeof(client_ip));
        }
        std::string ip_str(client_ip);

        // Rate limit check
        if (!g_rate_limiter.allow(ip_str)) {
            HttpResponse resp{.status_code = 429,
                              .status_text = "Too Many Requests",
                              .body = "{\"error\": \"Rate limit exceeded. Maximum 10 QPS allowed.\"}"};
            std::string raw_resp = resp.to_http_string();
            send(client_fd, raw_resp.data(), static_cast<send_len_t>(raw_resp.size()), 0);
#if defined(_WIN32) || defined(_WIN64)
            shutdown(client_fd, SD_BOTH);
            closesocket(client_fd);
#else
            shutdown(client_fd, SHUT_RDWR);
            close(client_fd);
#endif
            continue;
        }

        // Enqueue to worker pool
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (task_queue_.size() >= MAX_QUEUE_SIZE) {
                // Saturated -> return 503 and close
                HttpResponse resp{.status_code = 503,
                                  .status_text = "Service Unavailable",
                                  .body = "{\"error\": \"Server worker queue full.\"}"};
                std::string raw_resp = resp.to_http_string();
                send(client_fd, raw_resp.data(), static_cast<send_len_t>(raw_resp.size()), 0);
#if defined(_WIN32) || defined(_WIN64)
                shutdown(client_fd, SD_BOTH);
                closesocket(client_fd);
#else
                shutdown(client_fd, SHUT_RDWR);
                close(client_fd);
#endif
                continue;
            }
            task_queue_.push(static_cast<uintptr_t>(client_fd));
            queue_cv_.notify_one();
        }
    }
}

void HttpServer::stop() {
    if (is_running_.exchange(false)) {
        stop_workers_.store(true);
        if (server_socket_ != static_cast<uintptr_t>(INVALID_SOCK)) {
#if defined(_WIN32) || defined(_WIN64)
            closesocket(static_cast<socket_t>(server_socket_));
#else
            close(static_cast<socket_t>(server_socket_));
#endif
            server_socket_ = static_cast<uintptr_t>(INVALID_SOCK);
        }

        // Drain any pending queue tasks and notify all workers
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!task_queue_.empty()) {
                uintptr_t s = task_queue_.front();
                task_queue_.pop();
                socket_t c_fd = static_cast<socket_t>(s);
#if defined(_WIN32) || defined(_WIN64)
                shutdown(c_fd, SD_BOTH);
                closesocket(c_fd);
#else
                shutdown(c_fd, SHUT_RDWR);
                close(c_fd);
#endif
            }
            queue_cv_.notify_all();
        }

        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
        workers_.clear();
    }
}

}  // namespace needlefish
