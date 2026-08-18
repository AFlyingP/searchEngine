#include <algorithm>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

#include "invidx/radix_trie.hpp"

using namespace needlefish;

TEST(RadixTrieTest, BasicInsertAndLookup) {
    RadixTrie trie;
    trie.insert("apple", TermPayload{.term_id = 1, .doc_freq = 10});
    trie.insert("app", TermPayload{.term_id = 2, .doc_freq = 20});
    trie.insert("application", TermPayload{.term_id = 3, .doc_freq = 30});
    trie.insert("banana", TermPayload{.term_id = 4, .doc_freq = 40});
    trie.insert("band", TermPayload{.term_id = 5, .doc_freq = 50});

    auto p1 = trie.lookup("apple");
    EXPECT_TRUE(p1.valid());
    EXPECT_EQ(p1.term_id, 1);
    EXPECT_EQ(p1.doc_freq, 10);

    auto p2 = trie.lookup("app");
    EXPECT_TRUE(p2.valid());
    EXPECT_EQ(p2.term_id, 2);

    auto p3 = trie.lookup("application");
    EXPECT_TRUE(p3.valid());
    EXPECT_EQ(p3.term_id, 3);

    auto p_none = trie.lookup("appl");
    EXPECT_FALSE(p_none.valid());

    auto p_nonexistent = trie.lookup("cat");
    EXPECT_FALSE(p_nonexistent.valid());
}

TEST(RadixTrieTest, PrefixSearch) {
    RadixTrie trie;
    trie.insert("search", TermPayload{.term_id = 1});
    trie.insert("searching", TermPayload{.term_id = 2});
    trie.insert("searcher", TermPayload{.term_id = 3});
    trie.insert("season", TermPayload{.term_id = 4});

    auto results = trie.prefix_search("search", 10);
    EXPECT_EQ(results.size(), 3);

    auto sea_results = trie.prefix_search("sea", 10);
    EXPECT_EQ(sea_results.size(), 4);

    auto non_results = trie.prefix_search("xyz", 10);
    EXPECT_TRUE(non_results.empty());
}

TEST(RadixTrieTest, SerializationRoundtrip) {
    RadixTrie trie;
    trie.insert("compression", TermPayload{.term_id = 1, .doc_freq = 100, .postings_offset = 500});
    trie.insert("compact", TermPayload{.term_id = 2, .doc_freq = 200, .postings_offset = 1200});
    trie.insert("trie", TermPayload{.term_id = 3, .doc_freq = 300, .postings_offset = 2500});

    std::stringstream ss;
    trie.serialize(ss);

    RadixTrie loaded = RadixTrie::deserialize(ss);

    auto p1 = loaded.lookup("compression");
    EXPECT_TRUE(p1.valid());
    EXPECT_EQ(p1.term_id, 1);
    EXPECT_EQ(p1.doc_freq, 100);
    EXPECT_EQ(p1.postings_offset, 500);

    auto p2 = loaded.lookup("compact");
    EXPECT_TRUE(p2.valid());
    EXPECT_EQ(p2.term_id, 2);

    auto p3 = loaded.lookup("trie");
    EXPECT_TRUE(p3.valid());
    EXPECT_EQ(p3.term_id, 3);
}
