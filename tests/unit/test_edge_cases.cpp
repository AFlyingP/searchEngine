#include <gtest/gtest.h>
#include "invidx/index_builder.hpp"
#include "store/index_file.hpp"
#include "rank/query_eval.hpp"
#include "automata/levenshtein.hpp"
#include "util/analyzer.hpp"
#include <filesystem>
#include <fstream>
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
