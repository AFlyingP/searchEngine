#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rank/hybrid_search.hpp"
#include "store/index_file.hpp"

namespace needlefish {

struct HttpRequest {
    std::string method{};
    std::string path{};
    std::string raw_query{};
    std::unordered_map<std::string, std::string> query_params{};
    std::unordered_map<std::string, std::string> headers{};
    std::string body{};
};

struct HttpResponse {
    int status_code{200};
    std::string status_text{"OK"};
    std::string content_type{"application/json; charset=utf-8"};
    std::string body{};
    std::vector<std::pair<std::string, std::string>> headers{};

    [[nodiscard]] std::string to_http_string() const;
};

/**
 * @brief Zero-external-dependency embedded HTTP 1.1 server.
 * Supports cross-platform POSIX and Win32 sockets, routing REST APIs
 * and serving static UI assets.
 */
class HttpServer {
  public:
    HttpServer(IndexView& index, std::string host = "127.0.0.1", uint16_t port = 8080);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer(HttpServer&&) noexcept;
    HttpServer& operator=(HttpServer&&) noexcept;

    void set_static_directory(std::string dir_path) { static_dir_ = std::move(dir_path); }

    /**
     * @brief Handle an incoming raw HTTP request buffer and produce an HTTP response.
     */
    [[nodiscard]] HttpResponse handle_request(const HttpRequest& req) const;

    /**
     * @brief Parse an HTTP request string.
     */
    static HttpRequest parse_request(std::string_view raw_request);

    /**
     * @brief Start listening and handling requests synchronously.
     */
    void start();

    /**
     * @brief Stop the server.
     */
    void stop();

    [[nodiscard]] bool is_running() const noexcept { return is_running_.load(); }
    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] std::string_view host() const noexcept { return host_; }

  private:
    [[nodiscard]] HttpResponse handle_api_search(const HttpRequest& req) const;
    [[nodiscard]] HttpResponse handle_api_suggest(const HttpRequest& req) const;
    [[nodiscard]] HttpResponse handle_api_stats(const HttpRequest& req) const;
    [[nodiscard]] HttpResponse handle_api_health(const HttpRequest& req) const;
    [[nodiscard]] HttpResponse handle_static_file(std::string_view path) const;

    IndexView& index_;
    mutable HybridSearchEngine engine_;
    std::string host_;
    uint16_t port_;
    std::string static_dir_{"web"};
    std::atomic<bool> is_running_{false};
    uintptr_t server_socket_{static_cast<uintptr_t>(~0ULL)};
    std::chrono::steady_clock::time_point start_time_{std::chrono::steady_clock::now()};
};

}  // namespace needlefish
