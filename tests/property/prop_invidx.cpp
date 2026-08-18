#include <algorithm>
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "invidx/index_builder.hpp"
#include "rank/query_eval.hpp"
#include "store/index_file.hpp"

using namespace needlefish;

class InvertedIndexPropertyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "needlefish_prop_idx";
        std::filesystem::create_directories(temp_dir_);
        idx_path_ = temp_dir_ / "prop_corpus.idx";

        // Generate synthetic vocabulary of 50 distinct words
        std::vector<std::string> vocab;
        for (int i = 0; i < 50; ++i) {
            vocab.push_back("term" + std::to_string(i));
        }

        std::mt19937 rng(1337);
        IndexBuilder builder;

        // Build 100 documents of varying lengths
        for (uint32_t doc_id = 0; doc_id < 100; ++doc_id) {
            const size_t doc_len = 20 + (rng() % 80);
            std::stringstream text_ss;
            for (size_t w = 0; w < doc_len; ++w) {
                text_ss << vocab[rng() % vocab.size()] << " ";
            }
            std::string text = text_ss.str();
            std::string title = "Doc " + std::to_string(doc_id);
            builder.add_document(doc_id, title, text);
        }

        builder.write_index(idx_path_);
        index_.open(idx_path_);
    }

    void TearDown() override {
        index_ = IndexView{};
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    std::filesystem::path temp_dir_;
    std::filesystem::path idx_path_;
    IndexView index_;
};

TEST_F(InvertedIndexPropertyTest, WandVsNaiveScoringEquivalence1000Queries) {
    QueryEvaluator evaluator(index_);
    std::mt19937 rng(42);

    for (int q = 0; q < 1000; ++q) {
        const size_t num_terms = 1 + (rng() % 5);
        std::vector<std::string> terms;
        for (size_t t = 0; t < num_terms; ++t) {
            terms.push_back("term" + std::to_string(rng() % 50));
        }

        const size_t k = 1 + (rng() % 15);

        auto wand_res = evaluator.search_disjunction(terms, k, /*use_wand=*/true);
        auto naive_res = evaluator.search_disjunction(terms, k, /*use_wand=*/false);

        ASSERT_EQ(wand_res.hits.size(), naive_res.hits.size())
            << "Mismatch in result count for query " << q;

        for (size_t i = 0; i < wand_res.hits.size(); ++i) {
            EXPECT_NEAR(wand_res.hits[i].score, naive_res.hits[i].score, 1e-4f)
                << "Score mismatch at rank " << i << " for query " << q;
        }

        // Verify that for all strictly higher scores (not tied with the k-th boundary),
        // the sets of matching doc_ids are identical.
        if (!wand_res.hits.empty()) {
            const float min_score = wand_res.hits.back().score;
            std::vector<uint32_t> wand_strict, naive_strict;
            for (const auto& h : wand_res.hits) {
                if (h.score > min_score + 1e-4f)
                    wand_strict.push_back(h.doc_id);
            }
            for (const auto& h : naive_res.hits) {
                if (h.score > min_score + 1e-4f)
                    naive_strict.push_back(h.doc_id);
            }
            std::sort(wand_strict.begin(), wand_strict.end());
            std::sort(naive_strict.begin(), naive_strict.end());
            EXPECT_EQ(wand_strict, naive_strict) << "Strictly higher docs mismatch for query " << q;
        }
    }
}
