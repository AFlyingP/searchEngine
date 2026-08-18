#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "invidx/index_builder.hpp"
#include "rank/query_eval.hpp"
#include "rank/snippet.hpp"
#include "store/index_file.hpp"

using namespace needlefish;

class QueryEvalTest : public ::testing::Test {
  protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "needlefish_test_idx";
        std::filesystem::create_directories(temp_dir_);
        idx_path_ = temp_dir_ / "test_corpus.idx";

        // Build sample index
        IndexBuilder builder;
        builder.add_document(0, "Information Retrieval Basics",
                             "Information retrieval is the activity of obtaining information "
                             "system resources relevant to an information need.");
        builder.add_document(1, "Suffix Array and FM-Index",
                             "The suffix array is a sorted array of all suffixes of a string. The "
                             "FM-index combines suffix array with BWT.");
        builder.add_document(2, "Wavelet Tree Algorithms",
                             "Wavelet trees provide rank and select operations over arbitrary "
                             "alphabets in succinct space.");
        builder.add_document(3, "Search Engine Architecture",
                             "Modern search engine architecture combines inverted index with BM25 "
                             "scoring and WAND pruning.");
        builder.add_document(
            4, "Data Compression and BitVectors",
            "BitVectors with rank select support efficient compression and information storage.");

        builder.write_index(idx_path_);
        index_.open(idx_path_);
    }

    void TearDown() override {
        // Close index view before removing files
        index_ = IndexView{};
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    std::filesystem::path temp_dir_;
    std::filesystem::path idx_path_;
    IndexView index_;
};

TEST_F(QueryEvalTest, IndexLoadTimeUnder50ms) {
    const auto start = std::chrono::high_resolution_clock::now();
    IndexView fast_view(idx_path_);
    const auto end = std::chrono::high_resolution_clock::now();
    const auto load_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    EXPECT_LT(load_us, 50000);  // Load time < 50 ms
    EXPECT_EQ(fast_view.total_docs(), 5);
}

TEST_F(QueryEvalTest, WandVsNaiveScoringEquivalence) {
    QueryEvaluator evaluator(index_);
    std::vector<std::string> query_terms = {"information", "retrieval", "search", "engine"};

    auto wand_res = evaluator.search_disjunction(query_terms, 5, /*use_wand=*/true);
    auto naive_res = evaluator.search_disjunction(query_terms, 5, /*use_wand=*/false);

    ASSERT_EQ(wand_res.hits.size(), naive_res.hits.size());
    for (size_t i = 0; i < wand_res.hits.size(); ++i) {
        EXPECT_EQ(wand_res.hits[i].doc_id, naive_res.hits[i].doc_id);
        EXPECT_NEAR(wand_res.hits[i].score, naive_res.hits[i].score, 1e-4f);
    }
}

TEST_F(QueryEvalTest, BooleanAndConjunction) {
    QueryEvaluator evaluator(index_);
    std::vector<std::string> terms = {"suffix", "array"};

    auto res = evaluator.search_conjunction(terms, 5);
    ASSERT_EQ(res.hits.size(), 1);
    EXPECT_EQ(res.hits[0].doc_id, 1);
}

TEST_F(QueryEvalTest, PositionalPhraseQuery) {
    QueryEvaluator evaluator(index_);

    // Exact phrase in doc 1: "suffix array"
    std::vector<std::string> phrase = {"suffix", "array"};
    auto res = evaluator.search_phrase(phrase, 5);
    ASSERT_GE(res.hits.size(), 1);
    EXPECT_EQ(res.hits[0].doc_id, 1);

    // Non-consecutive phrase should not match
    std::vector<std::string> non_phrase = {"suffix", "combines"};
    auto non_res = evaluator.search_phrase(non_phrase, 5);
    EXPECT_EQ(non_res.hits.size(), 0);
}

TEST_F(QueryEvalTest, SnippetGeneration) {
    SnippetGenerator snippet_gen;
    std::string text = std::string(index_.doc_text(0));
    std::vector<std::string> query_terms = {"information", "retrieval"};

    std::string snippet = snippet_gen.highlight(text, query_terms);
    EXPECT_NE(snippet.find("<em>"), std::string::npos);
    EXPECT_NE(snippet.find("</em>"), std::string::npos);
}
