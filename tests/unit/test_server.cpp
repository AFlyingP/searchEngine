#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "invidx/index_builder.hpp"
#include "server/http_server.hpp"
#include "store/index_file.hpp"

using namespace needlefish;

class ServerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "needlefish_server_test";
        std::filesystem::create_directories(test_dir_);
        jsonl_path_ = test_dir_ / "docs.jsonl";
        idx_path_ = test_dir_ / "index.idx";

        std::ofstream jsonl(jsonl_path_);
        jsonl << "{\"id\": 1, \"title\": \"Operating Systems\", \"text\": \"Monolithic kernels "
                 "provide virtual memory and device drivers.\"}\n";
        jsonl << "{\"id\": 2, \"title\": \"Search Engines\", \"text\": \"Information retrieval "
                 "with inverted indexes and BM25 ranking.\"}\n";
        jsonl << "{\"id\": 3, \"title\": \"Succinct Indexing\", \"text\": \"Wavelet trees and "
                 "FM-index enable compressed text search.\"}\n";
        jsonl.close();

        IndexBuilder builder;
        builder.set_enable_fm_index(true);
        builder.index_jsonl_file(jsonl_path_);
        builder.write_index(idx_path_);

        index_view_ = std::make_unique<IndexView>(idx_path_);
        server_ = std::make_unique<HttpServer>(*index_view_, "127.0.0.1", 8080);
    }

    void TearDown() override {
        server_.reset();
        index_view_.reset();
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path jsonl_path_;
    std::filesystem::path idx_path_;
    std::unique_ptr<IndexView> index_view_;
    std::unique_ptr<HttpServer> server_;
};

TEST_F(ServerTest, ParseRequest) {
    std::string raw =
        "GET /api/search?q=virtual+memory&limit=5 HTTP/1.1\r\nHost: localhost:8080\r\n\r\n";
    HttpRequest req = HttpServer::parse_request(raw);

    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.path, "/api/search");
    EXPECT_EQ(req.query_params["q"], "virtual memory");
    EXPECT_EQ(req.query_params["limit"], "5");
}

TEST_F(ServerTest, ApiSearchSuccess) {
    HttpRequest req;
    req.method = "GET";
    req.path = "/api/search";
    req.query_params["q"] = "virtual memory";
    req.query_params["limit"] = "5";

    HttpResponse resp = server_->handle_request(req);
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_EQ(resp.status_text, "OK");
    EXPECT_TRUE(resp.body.find("\"total_hits\": 1") != std::string::npos);
    EXPECT_TRUE(resp.body.find("Operating Systems") != std::string::npos);
    EXPECT_TRUE(resp.body.find("took_us") != std::string::npos);
}

TEST_F(ServerTest, ApiSearchMissingParam) {
    HttpRequest req;
    req.method = "GET";
    req.path = "/api/search";

    HttpResponse resp = server_->handle_request(req);
    EXPECT_EQ(resp.status_code, 400);
    EXPECT_TRUE(resp.body.find("error") != std::string::npos);
}

TEST_F(ServerTest, ApiSuggest) {
    HttpRequest req;
    req.method = "GET";
    req.path = "/api/suggest";
    req.query_params["q"] = "monolith";

    HttpResponse resp = server_->handle_request(req);
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_TRUE(resp.body.find("monolith") != std::string::npos);
}

TEST_F(ServerTest, ApiStats) {
    HttpRequest req;
    req.method = "GET";
    req.path = "/api/stats";

    HttpResponse resp = server_->handle_request(req);
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_TRUE(resp.body.find("\"total_docs\": 3") != std::string::npos);
    EXPECT_TRUE(resp.body.find("\"has_fm_index\": true") != std::string::npos);
}

TEST_F(ServerTest, StaticFileNotFound) {
    HttpRequest req;
    req.method = "GET";
    req.path = "/nonexistent_file_12345.html";

    HttpResponse resp = server_->handle_request(req);
    EXPECT_EQ(resp.status_code, 404);
}
