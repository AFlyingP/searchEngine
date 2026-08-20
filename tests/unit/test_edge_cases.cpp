#include <gtest/gtest.h>
#include "invidx/index_builder.hpp"
#include "invidx/radix_trie.hpp"
#include "store/index_file.hpp"
#include "rank/query_eval.hpp"
#include "automata/levenshtein.hpp"
#include "automata/regex.hpp"
#include "server/http_server.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

class EdgeCasesTest : public ::testing::Test {
protected:
    std::string tmp_jsonl = "edge_case_corpus.jsonl";
    std::string tmp_idx = "edge_case.idx";

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(tmp_jsonl, ec);
        std::filesystem::remove(tmp_idx, ec);
    }
};

// 1. Empty index (0 documents)
TEST_F(EdgeCasesTest, EmptyIndexHandling) {
    {
        std::ofstream out(tmp_jsonl);
        // empty file
    }

    needlefish::IndexBuilder builder;
    builder.index_jsonl_file(tmp_jsonl);
    builder.write_index(tmp_idx);

    needlefish::IndexView index;
    ASSERT_NO_THROW(index.open(tmp_idx));
    EXPECT_EQ(index.stats().total_docs, 0u);

    needlefish::QueryEvaluator eval(index);
    auto results = eval.search("anything", 10);
    EXPECT_TRUE(results.hits.empty());
}

// 2. Empty query & whitespace-only query
TEST_F(EdgeCasesTest, EmptyAndWhitespaceQueries) {
    {
        std::ofstream out(tmp_jsonl);
        out << "{\"id\": 1, \"title\": \"Doc1\", \"text\": \"sample content here\"}\n";
    }

    needlefish::IndexBuilder builder;
    builder.index_jsonl_file(tmp_jsonl);
    builder.write_index(tmp_idx);

    needlefish::IndexView index;
    ASSERT_NO_THROW(index.open(tmp_idx));

    needlefish::QueryEvaluator eval(index);
    EXPECT_TRUE(eval.search("", 10).hits.empty());
    EXPECT_TRUE(eval.search("   ", 10).hits.empty());
    EXPECT_TRUE(eval.search("\t\n\r", 10).hits.empty());
}

// 3. 1-char terms with k=2 fuzzy search
TEST_F(EdgeCasesTest, SingleCharFuzzyMaxDistance) {
    {
        std::ofstream out(tmp_jsonl);
        out << "{\"id\": 1, \"title\": \"Doc1\", \"text\": \"a b c apple bat cat dog\"}\n";
    }

    needlefish::IndexBuilder builder;
    builder.index_jsonl_file(tmp_jsonl);
    builder.write_index(tmp_idx);

    needlefish::IndexView index;
    ASSERT_NO_THROW(index.open(tmp_idx));

    needlefish::LevenshteinAutomaton dfa("a", 2);
    auto start = dfa.initial_state();
    EXPECT_TRUE(dfa.can_match(start));

    // "apple" is distance 4 from "a", should not match
    auto state = start;
    for (char c : std::string("apple")) {
        state = dfa.step(state, c);
    }
    EXPECT_FALSE(dfa.is_accept(state));

    // "ab" is distance 1 from "a", should match
    state = start;
    for (char c : std::string("ab")) {
        state = dfa.step(state, c);
    }
    EXPECT_TRUE(dfa.is_accept(state));
}

// 4. Corpus of one repeated term
TEST_F(EdgeCasesTest, RepeatedTermCorpus) {
    {
        std::ofstream out(tmp_jsonl);
        for (int i = 1; i <= 20; ++i) {
            out << "{\"id\": " << i << ", \"title\": \"Doc" << i << "\", \"text\": \"";
            for (int j = 0; j < 50; ++j) {
                out << "needlefish ";
            }
            out << "\"}\n";
        }
    }

    needlefish::IndexBuilder builder;
    builder.index_jsonl_file(tmp_jsonl);
    builder.write_index(tmp_idx);

    needlefish::IndexView index;
    ASSERT_NO_THROW(index.open(tmp_idx));
    EXPECT_EQ(index.stats().total_docs, 20u);

    needlefish::QueryEvaluator eval(index);
    auto results = eval.search("needlefish", 10);
    EXPECT_EQ(results.hits.size(), 10u);
}

// 5. Document with no trailing newline
TEST_F(EdgeCasesTest, NoTrailingNewline) {
    {
        std::ofstream out(tmp_jsonl, std::ios::binary);
        out << "{\"id\": 1, \"title\": \"Doc1\", \"text\": \"valid line\"}";
    }

    needlefish::IndexBuilder builder;
    builder.index_jsonl_file(tmp_jsonl);
    builder.write_index(tmp_idx);

    needlefish::IndexView index;
    ASSERT_NO_THROW(index.open(tmp_idx));
    EXPECT_EQ(index.stats().total_docs, 1u);

    needlefish::QueryEvaluator eval(index);
    auto results = eval.search("valid", 10);
    EXPECT_EQ(results.hits.size(), 1u);
}

// 6. Long query resilience
TEST_F(EdgeCasesTest, ExtremelyLongQuery) {
    {
        std::ofstream out(tmp_jsonl);
        out << "{\"id\": 1, \"title\": \"Doc1\", \"text\": \"c++ search engine architecture\"}\n";
    }

    needlefish::IndexBuilder builder;
    builder.index_jsonl_file(tmp_jsonl);
    builder.write_index(tmp_idx);

    needlefish::IndexView index;
    ASSERT_NO_THROW(index.open(tmp_idx));

    std::string long_query;
    for (int i = 0; i < 500; ++i) {
        long_query += "term" + std::to_string(i) + " ";
    }
    long_query += "architecture";

    needlefish::QueryEvaluator eval(index);
    auto results = eval.search(long_query, 10);
    EXPECT_FALSE(results.hits.empty());
}

// 7. Bug 1 Regression: Corrupt RadixTrie node edge offset beyond string pool
TEST_F(EdgeCasesTest, CorruptRadixTrieNodeOffsetRejected) {
    needlefish::RadixTrie trie;
    trie.insert("algorithm", needlefish::TermPayload{.term_id = 0, .doc_freq = 1});

    std::stringstream ss;
    trie.serialize(ss);
    std::string serialized = ss.str();

    // Modify first node's edge_offset to point beyond pool size
    // RadixTrie header: num_nodes (8B), pool_size (8B), num_terms (8B) -> nodes start at offset 24
    if (serialized.size() >= 28) {
        uint32_t bad_offset = 0x7FFFFF00;
        std::memcpy(&serialized[24], &bad_offset, sizeof(bad_offset));
    }

    std::stringstream corrupt_ss(serialized);
    EXPECT_THROW(needlefish::RadixTrie::deserialize(corrupt_ss), std::runtime_error);
}

// 8. Bug 2 Regression: Section integer overflow bounds
TEST_F(EdgeCasesTest, CorruptSectionIntegerOverflowRejected) {
    // Construct a synthetic 100-byte index file with wrapping uint64 section bounds
    std::vector<uint8_t> malformed(128, 0);
    needlefish::IndexHeader header;
    std::memcpy(header.magic, needlefish::INDEX_MAGIC, sizeof(needlefish::INDEX_MAGIC));
    header.version = needlefish::INDEX_VERSION;
    header.num_sections = 1;
    std::memcpy(malformed.data(), &header, sizeof(header));

    needlefish::SectionEntry sec;
    sec.section_id = 0;
    sec.offset = 0xFFFFFFFFFFFFFF00ULL; // Large offset that wraps
    sec.length = 0x200;                 // Length causing wrap
    sec.checksum = 0;
    std::memcpy(malformed.data() + sizeof(header), &sec, sizeof(sec));

    {
        std::ofstream out(tmp_idx, std::ios::binary);
        out.write(reinterpret_cast<const char*>(malformed.data()), malformed.size());
    }

    needlefish::IndexView index;
    EXPECT_THROW(index.open(tmp_idx), std::runtime_error);
}

// 9. Bug 3 Regression: Static file path traversal blocked
TEST_F(EdgeCasesTest, StaticFilePathTraversalBlocked) {
    {
        std::ofstream out(tmp_jsonl);
        out << "{\"id\": 1, \"title\": \"Doc1\", \"text\": \"sample content\"}\n";
    }
    needlefish::IndexBuilder builder;
    builder.index_jsonl_file(tmp_jsonl);
    builder.write_index(tmp_idx);

    needlefish::IndexView index;
    index.open(tmp_idx);

    needlefish::HttpServer server(index, "127.0.0.1", 8080);
    server.set_static_directory(".");

    // Directly test handle_request with path traversal payload
    needlefish::HttpRequest req;
    req.method = "GET";
    req.path = "/../../../../windows/win.ini";
    auto resp = server.handle_request(req);

    EXPECT_EQ(resp.status_code, 403);
}

// 10. Regex recursion depth limit test
TEST_F(EdgeCasesTest, RegexRecursionDepthLimit) {
    std::string deep_regex;
    for (int i = 0; i < 100; ++i) {
        deep_regex += "a|";
    }
    deep_regex += "b";

    EXPECT_THROW(needlefish::Regex r(deep_regex), std::runtime_error);
}
