#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "automata/autocomplete.hpp"
#include "automata/levenshtein.hpp"
#include "automata/regex.hpp"
#include "invidx/index_builder.hpp"
#include "rank/hybrid_search.hpp"
#include "store/index_file.hpp"

using namespace needlefish;

TEST(RegexTest, BasicLiteralsAndConcat) {
    Regex r("hello");
    EXPECT_TRUE(r.is_match("hello"));
    EXPECT_FALSE(r.is_match("hell"));
    EXPECT_FALSE(r.is_match("hello world"));

    auto m = r.find_first("say hello to the world");
    EXPECT_EQ(m.first, 4);
    EXPECT_EQ(m.second, 9);
}

TEST(RegexTest, WildcardAndCharClasses) {
    Regex r("h.llo");
    EXPECT_TRUE(r.is_match("hello"));
    EXPECT_TRUE(r.is_match("hallo"));
    EXPECT_TRUE(r.is_match("hxllo"));
    EXPECT_FALSE(r.is_match("hllo"));

    Regex r_class("[a-z0-9_]+");
    EXPECT_TRUE(r_class.is_match("hello_123"));
    EXPECT_FALSE(r_class.is_match("hello-123"));

    Regex r_neg("[^0-9]+");
    EXPECT_TRUE(r_neg.is_match("abcXYZ"));
    EXPECT_FALSE(r_neg.is_match("abc1XYZ"));
}

TEST(RegexTest, QuantifiersAndRepetitions) {
    Regex r_star("ab*c");
    EXPECT_TRUE(r_star.is_match("ac"));
    EXPECT_TRUE(r_star.is_match("abc"));
    EXPECT_TRUE(r_star.is_match("abbbbc"));
    EXPECT_FALSE(r_star.is_match("abbbb"));

    Regex r_plus("ab+c");
    EXPECT_FALSE(r_plus.is_match("ac"));
    EXPECT_TRUE(r_plus.is_match("abc"));
    EXPECT_TRUE(r_plus.is_match("abbc"));

    Regex r_q("ab?c");
    EXPECT_TRUE(r_q.is_match("ac"));
    EXPECT_TRUE(r_q.is_match("abc"));
    EXPECT_FALSE(r_q.is_match("abbc"));

    Regex r_count("a{2,4}");
    EXPECT_FALSE(r_count.is_match("a"));
    EXPECT_TRUE(r_count.is_match("aa"));
    EXPECT_TRUE(r_count.is_match("aaa"));
    EXPECT_TRUE(r_count.is_match("aaaa"));
    EXPECT_FALSE(r_count.is_match("aaaaa"));
}

TEST(RegexTest, AlternationAndGrouping) {
    Regex r("(cat|dog)s?");
    EXPECT_TRUE(r.is_match("cat"));
    EXPECT_TRUE(r.is_match("cats"));
    EXPECT_TRUE(r.is_match("dog"));
    EXPECT_TRUE(r.is_match("dogs"));
    EXPECT_FALSE(r.is_match("bird"));
    EXPECT_FALSE(r.is_match("catdog"));
}

TEST(RegexTest, FindAllMatches) {
    Regex r("\\d+");
    std::string text = "Items: 42 apples, 100 oranges, 9 bananas";
    auto matches = r.find_all(text);
    ASSERT_EQ(matches.size(), 3);
    EXPECT_EQ(text.substr(matches[0].first, matches[0].second - matches[0].first), "42");
    EXPECT_EQ(text.substr(matches[1].first, matches[1].second - matches[1].first), "100");
    EXPECT_EQ(text.substr(matches[2].first, matches[2].second - matches[2].first), "9");
}

TEST(LevenshteinTest, ExactAndTypoTransitions) {
    LevenshteinAutomaton dfa("algorithm", 2);

    auto s = dfa.initial_state();
    for (char c : std::string("algorithm")) {
        s = dfa.step(s, c);
    }
    EXPECT_TRUE(dfa.is_accept(s));
    EXPECT_EQ(dfa.distance(s), 0);

    // Substitution: "algorythm" (1 substitution)
    s = dfa.initial_state();
    for (char c : std::string("algorythm")) {
        s = dfa.step(s, c);
    }
    EXPECT_TRUE(dfa.is_accept(s));
    EXPECT_EQ(dfa.distance(s), 1);

    // Deletion: "algoritm" (missing 'h', 1 deletion)
    s = dfa.initial_state();
    for (char c : std::string("algoritm")) {
        s = dfa.step(s, c);
    }
    EXPECT_TRUE(dfa.is_accept(s));
    EXPECT_EQ(dfa.distance(s), 1);

    // Insertion: "algorithms" (1 insertion)
    s = dfa.initial_state();
    for (char c : std::string("algorithms")) {
        s = dfa.step(s, c);
    }
    EXPECT_TRUE(dfa.is_accept(s));
    EXPECT_EQ(dfa.distance(s), 1);

    // Too far: "somethingelse"
    s = dfa.initial_state();
    for (char c : std::string("somethingelse")) {
        s = dfa.step(s, c);
    }
    EXPECT_FALSE(dfa.is_accept(s));
}

TEST(LevenshteinTest, TrieIntersectionLockstep) {
    RadixTrie trie;
    trie.insert("search", TermPayload{.term_id = 1, .doc_freq = 50});
    trie.insert("searching", TermPayload{.term_id = 2, .doc_freq = 30});
    trie.insert("searches", TermPayload{.term_id = 3, .doc_freq = 20});
    trie.insert("bench", TermPayload{.term_id = 4, .doc_freq = 10});
    trie.insert("research", TermPayload{.term_id = 5, .doc_freq = 40});

    // Query typo "serach" (edit dist 2)
    LevenshteinAutomaton dfa("serach", 2);
    auto matches = dfa.match_trie(trie);

    bool found_search = false;
    for (const auto& m : matches) {
        if (m.term == "search") {
            found_search = true;
            EXPECT_LE(m.distance, 2);
        }
    }
    EXPECT_TRUE(found_search);
}

class HybridEngineTest : public ::testing::Test {
  protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "needlefish_hybrid_test";
        std::filesystem::create_directories(temp_dir_);
        idx_path_ = temp_dir_ / "test_corpus.idx";

        IndexBuilder builder;
        builder.set_enable_fm_index(true);

        builder.add_document(0, "Linux Kernel Systems",
                             "The Linux operating system kernel implements monolithic "
                             "architecture, processes, and virtual memory.");
        builder.add_document(1, "Information Retrieval",
                             "Search engines utilize inverted indexes and BM25 ranking algorithms "
                             "with Block-Max WAND dynamic pruning.");
        builder.add_document(2, "Succinct Data Structures",
                             "Wavelet trees and FM-indexes enable compact text indexing and "
                             "compressed pattern matching without decompression.");
        builder.add_document(3, "High Performance C++",
                             "Modern C++20 provides zero-copy memory-mapped IO, SIMD "
                             "vectorization, and lock-free concurrency.");

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

TEST_F(HybridEngineTest, AutocompletePrefixAndFuzzy) {
    AutocompleteEngine autocomplete(index_);

    auto prefix_res = autocomplete.prefix_suggest("infor", 5);
    ASSERT_FALSE(prefix_res.empty());
    EXPECT_EQ(prefix_res[0].text, "inform");

    auto fuzzy_res = autocomplete.fuzzy_suggest("sukcinct", 2, 5);
    ASSERT_FALSE(fuzzy_res.empty());
    EXPECT_EQ(fuzzy_res[0].text, "succinct");

    std::string correction = autocomplete.did_you_mean("sukcinct data structurs");
    EXPECT_FALSE(correction.empty());
    EXPECT_NE(correction.find("succinct"), std::string::npos);
}

TEST_F(HybridEngineTest, HybridSearchRouting) {
    HybridSearchEngine hybrid(index_);

    // 1. Standard BM25
    auto bm25_res = hybrid.search("information retrieval", 5);
    EXPECT_EQ(bm25_res.query_type, HybridQueryType::Standard);
    ASSERT_FALSE(bm25_res.hits.empty());
    EXPECT_EQ(bm25_res.hits[0].doc_id, 1);

    // 2. Fuzzy query with tilde
    auto fuzzy_res = hybrid.search("sukcinct~2", 5);
    EXPECT_EQ(fuzzy_res.query_type, HybridQueryType::Fuzzy);
    ASSERT_FALSE(fuzzy_res.hits.empty());
    EXPECT_EQ(fuzzy_res.hits[0].doc_id, 2);

    // 3. Regex query in slashes
    auto regex_res = hybrid.search("/C\\+\\+\\d+/", 5);
    EXPECT_EQ(regex_res.query_type, HybridQueryType::Regex);
    ASSERT_FALSE(regex_res.hits.empty());
    EXPECT_EQ(regex_res.hits[0].doc_id, 3);

    // 4. FM-Index Substring search
    EXPECT_TRUE(index_.has_fm_index());
    auto sub_res = hybrid.search_substring("monolithic architecture", 5);
    EXPECT_EQ(sub_res.query_type, HybridQueryType::Substring);
    ASSERT_FALSE(sub_res.hits.empty());
    EXPECT_EQ(sub_res.hits[0].doc_id, 0);
}
