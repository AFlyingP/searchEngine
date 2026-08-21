#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <sstream>
#include <vector>

#include "automata/levenshtein.hpp"
#include "automata/regex.hpp"
#include "needlefish_c.h"
#include "invidx/compression.hpp"
#include "invidx/index_builder.hpp"
#include "invidx/postings.hpp"
#include "rank/query_eval.hpp"
#include "rank/snippet.hpp"
#include "store/index_file.hpp"
#include "util/analyzer.hpp"

using namespace needlefish;

TEST(RegressionTest, SIMDVsScalarDifferentialUnpack) {
    std::mt19937_64 rng(42);
    alignas(64) uint32_t in_vals[128];
    alignas(64) uint8_t packed[128 * 4 + 64];
    alignas(64) uint32_t out_scalar[128];
    alignas(64) uint32_t out_simd[128];

    for (uint8_t bit_width = 0; bit_width <= 32; ++bit_width) {
        uint64_t mask = (bit_width == 32) ? 0xFFFFFFFFULL : ((1ULL << bit_width) - 1ULL);
        if (bit_width == 0) mask = 0;

        for (size_t i = 0; i < 128; ++i) {
            in_vals[i] = static_cast<uint32_t>(rng() & mask);
        }

        std::memset(packed, 0, sizeof(packed));
        BitPacking::pack128(in_vals, packed, bit_width);

        std::memset(out_scalar, 0xFF, sizeof(out_scalar));
        std::memset(out_simd, 0xEE, sizeof(out_simd));

        BitPacking::unpack128_scalar(packed, out_scalar, bit_width);
        BitPacking::unpack128_simd(packed, out_simd, bit_width);

        for (size_t i = 0; i < 128; ++i) {
            ASSERT_EQ(out_simd[i], in_vals[i])
                << "SIMD unpack failed at bit_width=" << (int)bit_width << " index=" << i;
            ASSERT_EQ(out_scalar[i], in_vals[i])
                << "Scalar unpack failed at bit_width=" << (int)bit_width << " index=" << i;
            ASSERT_EQ(out_simd[i], out_scalar[i])
                << "Mismatch between SIMD and scalar at bit_width=" << (int)bit_width << " index=" << i;
        }
    }
}

TEST(RegressionTest, VarintBoundsCheck) {
    uint8_t truncated[3] = {0x80, 0x80, 0x80};  // all continuation bytes without terminator
    uint32_t val = 0;
    size_t bytes = Varint::decode_uint32(truncated, truncated + 3, &val);
    EXPECT_LE(bytes, 3u);

    // Empty buffer
    bytes = Varint::decode_uint32(truncated, truncated, &val);
    EXPECT_EQ(bytes, 0u);
}

TEST(RegressionTest, PostingListWriterMonotonicity) {
    PostingListWriter writer;
    uint32_t pos[] = {1, 2};
    writer.add_posting(10, 2, pos);
    writer.add_posting(20, 2, pos);
    EXPECT_THROW(writer.add_posting(15, 2, pos), std::invalid_argument);
    EXPECT_THROW(writer.add_posting(20, 2, pos), std::invalid_argument);
}

TEST(RegressionTest, RegexLimits) {
    std::string giant(600, 'a');
    EXPECT_THROW(RegexParser::parse(giant), std::invalid_argument);

    EXPECT_THROW(RegexParser::parse("a{200}"), std::runtime_error);
    EXPECT_THROW(RegexParser::parse("a{10,5}"), std::runtime_error);
}

TEST(RegressionTest, SnippetHtmlEscaping) {
    SnippetGenerator gen(200);
    std::string text = "Click <script>alert('xss')</script> & enjoy \"fun\"";
    std::vector<std::string> query = {"fun"};
    std::string result = gen.highlight(text, query);

    EXPECT_EQ(result.find("<script>"), std::string::npos);
    EXPECT_NE(result.find("&lt;script&gt;"), std::string::npos);
    EXPECT_NE(result.find("&amp;"), std::string::npos);
    EXPECT_NE(result.find("&quot;<em>fun</em>&quot;"), std::string::npos);
}

TEST(RegressionTest, JsonlUnicodeUnescape) {
    auto tmp_jsonl = std::filesystem::temp_directory_path() / "test_reg_unicode.jsonl";
    {
        std::ofstream ofs(tmp_jsonl);
        ofs << "{\"id\": 42, \"title\": \"Unicode \\u0048\\u0065\\u006c\\u006c\\u006f\", \"text\": \"Doc with \\u0077\\u006f\\u0072\\u006c\\u0064 and quotes \\\"test\\\"\"}\n";
    }

    IndexBuilder builder;
    builder.index_jsonl_file(tmp_jsonl);
    auto tmp_path = std::filesystem::temp_directory_path() / "test_reg_unicode.idx";
    builder.write_index(tmp_path);

    {
        IndexView view(tmp_path);
        EXPECT_EQ(view.doc_metadata(0).doc_id, 42u);
        EXPECT_EQ(view.external_id(0), 42u);
        EXPECT_NE(view.doc_title(0).find("Hello"), std::string_view::npos);
        EXPECT_NE(view.doc_text(0).find("world"), std::string_view::npos);
    }
    std::filesystem::remove(tmp_jsonl);
    std::filesystem::remove(tmp_path);
}

TEST(RegressionTest, CApiExceptionSafetyAndExternalIds) {
    IndexBuilder builder;
    builder.add_document(999, "Quantum Physics", "Quantum mechanics explains particle nature.");
    auto tmp_path = std::filesystem::temp_directory_path() / "test_capi_reg.idx";
    builder.write_index(tmp_path);

    needlefish_index_t* idx = needlefish_open(tmp_path.string().c_str());
    ASSERT_NE(idx, nullptr);
    EXPECT_EQ(needlefish_total_docs(idx), 1u);

    auto* res = needlefish_search(idx, "quantum", 5);
    ASSERT_NE(res, nullptr);
    ASSERT_EQ(res->num_hits, 1u);
    EXPECT_EQ(res->hits[0].doc_id, 999u);  // External ID preserved!
    needlefish_free_search_result(res);

    needlefish_close(idx);
    std::filesystem::remove(tmp_path);

    // Null safety
    EXPECT_EQ(needlefish_open(nullptr), nullptr);
    needlefish_close(nullptr);
    EXPECT_EQ(needlefish_total_docs(nullptr), 0u);
    EXPECT_EQ(needlefish_search(nullptr, "test", 5), nullptr);
}

TEST(RegressionTest, NegationOnScoreDescendingHits) {
    auto tmp_idx = std::filesystem::temp_directory_path() / "test_neg_reg.idx";
    {
        IndexBuilder builder;
        builder.add_document(0, "Lucene Search", "lucene search engine index");
        builder.add_document(1, "Deep Index", "index index index indexing techniques");
        builder.add_document(2, "Database Index", "index database storage architecture");
        builder.write_index(tmp_idx);
    }

    {
        IndexView view(tmp_idx);
        QueryEvaluator evaluator(view);

        auto res = evaluator.search("index -lucene", 10);
        ASSERT_GE(res.hits.size(), 2u);
        for (const auto& hit : res.hits) {
            EXPECT_NE(hit.doc_id, 0u) << "Doc 0 containing negated term 'lucene' was not filtered!";
        }
    }

    std::filesystem::remove(tmp_idx);
}

TEST(RegressionTest, BareTermConjunctionVsExplicitOr) {
    auto tmp_idx = std::filesystem::temp_directory_path() / "test_and_or.idx";
    {
        IndexBuilder builder;
        builder.add_document(0, "Doc A", "apple banana fruit basket");
        builder.add_document(1, "Doc B", "apple orange citrus orchard");
        builder.add_document(2, "Doc C", "banana grape vineyard");
        builder.write_index(tmp_idx);
    }

    {
        IndexView view(tmp_idx);
        QueryEvaluator evaluator(view);

        // Bare terms: Boolean AND
        auto and_res = evaluator.search("apple banana", 10);
        ASSERT_EQ(and_res.hits.size(), 1u);
        EXPECT_EQ(and_res.hits[0].doc_id, 0u);

        // Explicit OR: Boolean Disjunction
        auto or_res = evaluator.search("apple OR banana", 10);
        ASSERT_EQ(or_res.hits.size(), 3u);
    }

    std::filesystem::remove(tmp_idx);
}

TEST(RegressionTest, PhraseNonIdempotentStemming) {
    auto tmp_idx = std::filesystem::temp_directory_path() / "test_phrase_stem.idx";
    {
        IndexBuilder builder;
        builder.add_document(0, "Title", "The king will abase the wicked ruler");
        builder.write_index(tmp_idx);
    }

    {
        IndexView view(tmp_idx);
        QueryEvaluator evaluator(view);

        auto res = evaluator.search("\"abase the\"", 5);
        ASSERT_EQ(res.hits.size(), 1u);
        EXPECT_EQ(res.hits[0].doc_id, 0u);
    }

    std::filesystem::remove(tmp_idx);
}

TEST(RegressionTest, TieBreakingDocIdAscendingDeterminism) {
    auto tmp_idx = std::filesystem::temp_directory_path() / "test_tiebreak.idx";
    {
        IndexBuilder builder;
        // Same length and same term counts -> equal BM25 scores
        builder.add_document(5, "Title 5", "matrix vector compute");
        builder.add_document(2, "Title 2", "matrix vector compute");
        builder.add_document(8, "Title 8", "matrix vector compute");
        builder.add_document(1, "Title 1", "matrix vector compute");
        builder.write_index(tmp_idx);
    }

    {
        IndexView view(tmp_idx);
        QueryEvaluator evaluator(view);

        // Disjunction / WAND
        auto disj_res = evaluator.search_disjunction(std::vector<std::string>{"matrix"}, 10, true);
        ASSERT_EQ(disj_res.hits.size(), 4u);
        for (size_t i = 1; i < disj_res.hits.size(); ++i) {
            if (disj_res.hits[i].score == disj_res.hits[i - 1].score) {
                EXPECT_LT(disj_res.hits[i - 1].doc_id, disj_res.hits[i].doc_id);
            }
        }

        // Conjunction
        auto conj_res = evaluator.search_conjunction(std::vector<std::string>{"matrix", "vector"}, 10);
        ASSERT_EQ(conj_res.hits.size(), 4u);
        for (size_t i = 1; i < conj_res.hits.size(); ++i) {
            if (conj_res.hits[i].score == conj_res.hits[i - 1].score) {
                EXPECT_LT(conj_res.hits[i - 1].doc_id, conj_res.hits[i].doc_id);
            }
        }

        // Phrase
        auto phrase_res = evaluator.search_phrase(std::vector<std::string>{"matrix", "vector"}, 10);
        ASSERT_EQ(phrase_res.hits.size(), 4u);
        for (size_t i = 1; i < phrase_res.hits.size(); ++i) {
            if (phrase_res.hits[i].score == phrase_res.hits[i - 1].score) {
                EXPECT_LT(phrase_res.hits[i - 1].doc_id, phrase_res.hits[i].doc_id);
            }
        }
    }

    std::filesystem::remove(tmp_idx);
}

TEST(RegressionTest, HeavyNegationPreservesFullKResults) {
    auto tmp_idx = std::filesystem::temp_directory_path() / "test_heavy_neg.idx";
    {
        IndexBuilder builder;
        for (uint32_t i = 0; i < 30; ++i) {
            if (i % 2 == 0) {
                builder.add_document(i, "Doc", "target common keyword excluded stopword");
            } else {
                builder.add_document(i, "Doc", "target common keyword allowed payload");
            }
        }
        builder.write_index(tmp_idx);
    }

    {
        IndexView view(tmp_idx);
        QueryEvaluator evaluator(view);

        auto res = evaluator.search("target common -stopword", 10);
        EXPECT_EQ(res.hits.size(), 10u);
        for (const auto& hit : res.hits) {
            EXPECT_EQ(hit.doc_id % 2, 1u);
        }
    }

    std::filesystem::remove(tmp_idx);
}

TEST(RegressionTest, RadixTrieDeserializerRejectionsAndEmptyGuard) {
    // 0 nodes rejection
    {
        std::stringstream ss;
        uint64_t num_n = 0;
        uint64_t pool_sz = 0;
        uint64_t num_t = 0;
        ss.write(reinterpret_cast<const char*>(&num_n), sizeof(num_n));
        ss.write(reinterpret_cast<const char*>(&pool_sz), sizeof(pool_sz));
        ss.write(reinterpret_cast<const char*>(&num_t), sizeof(num_t));

        EXPECT_THROW(RadixTrie::deserialize(ss), std::runtime_error);
    }

    // Empty trie lookup guard
    {
        RadixTrie empty_trie;
        EXPECT_FALSE(empty_trie.lookup("hello").valid());
        EXPECT_FALSE(empty_trie.lookup("").valid());
    }
}

TEST(RegressionTest, FMIndexDeserializerRejectsZeroSampleRate) {
    std::stringstream ss;
    uint64_t ts = 100, bs = 100, sr = 0, pi = 10;
    uint8_t is64 = 0;
    ss.write(reinterpret_cast<const char*>(&ts), sizeof(ts));
    ss.write(reinterpret_cast<const char*>(&bs), sizeof(bs));
    ss.write(reinterpret_cast<const char*>(&sr), sizeof(sr));
    ss.write(reinterpret_cast<const char*>(&pi), sizeof(pi));
    ss.write(reinterpret_cast<const char*>(&is64), sizeof(is64));

    EXPECT_THROW(FMIndex::deserialize(ss), std::runtime_error);
}

TEST(RegressionTest, SurrogatePairsAndLoneSurrogatesInJsonl) {
    auto tmp_jsonl = std::filesystem::temp_directory_path() / "test_surrogates.jsonl";
    {
        std::ofstream ofs(tmp_jsonl);
        // Valid surrogate pair \uD83D\uDE00 (grinning face U+1F600) and lone surrogate \uD800
        ofs << "{\"id\": 10, \"title\": \"Emoji \\uD83D\\uDE00 Test\", \"text\": \"Lone \\uD800 Surrogate\"}\n";
    }

    IndexBuilder builder;
    builder.index_jsonl_file(tmp_jsonl);
    auto tmp_idx = std::filesystem::temp_directory_path() / "test_surrogates.idx";
    builder.write_index(tmp_idx);

    {
        IndexView view(tmp_idx);
        EXPECT_NE(view.doc_title(0).find("\xF0\x9F\x98\x80"), std::string_view::npos);
        EXPECT_NE(view.doc_text(0).find("\xEF\xBF\xBD"), std::string_view::npos);
    }

    std::filesystem::remove(tmp_jsonl);
    std::filesystem::remove(tmp_idx);
}
