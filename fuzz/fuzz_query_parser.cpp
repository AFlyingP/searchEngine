#include "server/http_server.hpp"
#include "util/analyzer.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 4096) {
        return 0;
    }

    std::string_view input(reinterpret_cast<const char*>(data), size);

    // 1. Fuzz HTTP Request Parser
    auto req = needlefish::HttpServer::parse_request(input);
    (void)req.method;
    (void)req.path;
    (void)req.query_params;

    // 2. Fuzz Query Analyzer
    needlefish::Analyzer analyzer;
    (void)analyzer.normalize_term(input);

    return 0;
}

#ifndef NEEDLEFISH_LIBFUZZER
int main() {
    std::vector<std::string> test_inputs = {
        "",
        "GET /api/search?q=test HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "POST /api/search HTTP/1.1\r\nContent-Length: 15\r\n\r\n{\"query\":\"c++\"}",
        "GET /api/search?q=%20%FF%FE%00%2B&mode=fuzzy&limit=-1 HTTP/1.1\r\n\r\n",
        "MALFORMED_HEADER_WITHOUT_SPACES\r\n\r\n",
        "GET /../../secret HTTP/1.1\r\n\r\n",
        std::string(2000, 'A') + "?q=" + std::string(1000, '%')
    };

    for (const auto& input : test_inputs) {
        LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    }

    return 0;
}
#endif
