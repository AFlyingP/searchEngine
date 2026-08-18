#include <gtest/gtest.h>
#include "fm/fm_index.hpp"

#include <sstream>
#include <string>
#include <vector>

using namespace needlefish;

TEST(FMIndexTest, BasicCountAndLocate) {
    std::string text = "banana";
    FMIndex fmi(text, 2);

    EXPECT_EQ(fmi.size(), 6);
    EXPECT_EQ(fmi.count("a"), 3);
    EXPECT_EQ(fmi.count("ana"), 2);
    EXPECT_EQ(fmi.count("anana"), 1);
    EXPECT_EQ(fmi.count("banana"), 1);
    EXPECT_EQ(fmi.count("nan"), 1);
    EXPECT_EQ(fmi.count("xyz"), 0);

    auto occ_a = fmi.locate("a");
    std::vector<size_t> expected_a = {1, 3, 5};
    EXPECT_EQ(occ_a, expected_a);

    auto occ_ana = fmi.locate("ana");
    std::vector<size_t> expected_ana = {1, 3};
    EXPECT_EQ(occ_ana, expected_ana);
}

TEST(FMIndexTest, ExtractSnippet) {
    std::string text = "The quick brown fox jumps over the lazy dog";
    FMIndex fmi(text, 4);

    EXPECT_EQ(fmi.extract(0, 3), "The");
    EXPECT_EQ(fmi.extract(4, 9), "quick");
    EXPECT_EQ(fmi.extract(10, 15), "brown");
    EXPECT_EQ(fmi.extract(0, text.size()), text);
}

TEST(FMIndexTest, SerializationRoundtrip) {
    std::string text = "mississippi and missouri in mississippi basin";
    FMIndex fmi(text, 4);

    std::stringstream ss;
    fmi.serialize(ss);

    FMIndex restored = FMIndex::deserialize(ss);
    EXPECT_EQ(restored.size(), fmi.size());
    EXPECT_EQ(restored.count("mississippi"), 2);
    EXPECT_EQ(restored.count("in"), 2);

    EXPECT_EQ(restored.locate("mississippi"), fmi.locate("mississippi"));
    EXPECT_EQ(restored.extract(0, 11), "mississippi");
}
