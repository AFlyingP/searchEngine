#include <gtest/gtest.h>
#include "invidx/index_builder.hpp"
#include "store/index_file.hpp"
#include "rank/query_eval.hpp"
#include "automata/autocomplete.hpp"
#include "fm/fm_index.hpp"
#include <filesystem>
#include <fstream>
#include <string>

class IntegrationE2ETest : public ::testing::Test {
protected:
    std::string fixture_jsonl = "fixture_corpus.jsonl";
    std::string fixture_idx = "fixture.idx";

    void SetUp() override {
        std::ofstream out(fixture_jsonl);
        out << "{\"id\": 1, \"title\": \"Information Retrieval\", \"text\": \"Information retrieval is the science of searching for information in a document, searching for documents themselves, and also searching for the metadata that describes data.\"}\n";
        out << "{\"id\": 2, \"title\": \"Burrows Wheeler Transform\", \"text\": \"The Burrows-Wheeler transform rearranges a character string into runs of similar characters. This is useful for compression and FM-index pattern matching.\"}\n";
        out << "{\"id\": 3, \"title\": \"Levenshtein Automaton\", \"text\": \"A Levenshtein automaton recognizes all strings within a given edit distance k of a target pattern using universal parametric transitions.\"}\n";
        out << "{\"id\": 4, \"title\": \"Block-Max WAND Pruning\", \"text\": \"Block-Max WAND skips non-competitive posting blocks during disjunctive query evaluation, accelerating top-k retrieval by orders of magnitude.\"}\n";
        out << "{\"id\": 5, \"title\": \"Radix Trie Term Dictionary\", \"text\": \"A compressed Radix Trie stores the lexicon contiguously in memory for prefix matching, fuzzy intersection, and fast dictionary lookup.\"}\n";
        out.close();

        needlefish::IndexBuilder builder;
        builder.set_enable_fm_index(true);
        builder.index_jsonl_file(fixture_jsonl);
        builder.write_index(fixture_idx);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(fixture_jsonl, ec);
        std::filesystem::remove(fixture_idx, ec);
    }
};

TEST_F(IntegrationE2ETest, EndToEndBM25Search) {
    needlefish::IndexView index;
    ASSERT_NO_THROW(index.open(fixture_idx));
    EXPECT_EQ(index.stats().total_docs, 5u);

    needlefish::QueryEvaluator eval(index);

    // 1. BM25 Query
    auto res_ir = eval.search("information retrieval", 5);
    ASSERT_FALSE(res_ir.hits.empty());
    EXPECT_EQ(res_ir.hits[0].doc_id, 0u);
    EXPECT_EQ(index.doc_title(res_ir.hits[0].doc_id), "Information Retrieval");

    // 2. Phrase Query
    auto res_phrase = eval.search("\"edit distance\"", 5);
    ASSERT_FALSE(res_phrase.hits.empty());
    EXPECT_EQ(res_phrase.hits[0].doc_id, 2u);
    EXPECT_EQ(index.doc_title(res_phrase.hits[0].doc_id), "Levenshtein Automaton");

    // 3. FM-Index Substring Query
    if (index.fm_index()) {
        auto matches = index.fm_index()->locate("transform");
        EXPECT_FALSE(matches.empty());
    }

    // 4. Prefix Autocomplete
    needlefish::AutocompleteEngine auto_engine(index);
    auto suggestions = auto_engine.prefix_suggest("levensh", 5);
    ASSERT_FALSE(suggestions.empty());
    EXPECT_TRUE(suggestions[0].text.find("levenshtein") != std::string::npos);
}
