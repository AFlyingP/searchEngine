#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
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
