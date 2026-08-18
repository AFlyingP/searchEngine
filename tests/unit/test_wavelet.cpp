#include <gtest/gtest.h>
#include "wavelet/wavelet_tree.hpp"

#include <sstream>
#include <string>

using namespace needlefish;

TEST(WaveletTreeTest, BasicOperations) {
    std::string text = "abracadabra";
    WaveletTree wt(text);

    EXPECT_EQ(wt.size(), 11);
    EXPECT_FALSE(wt.empty());

    // Access
    for (size_t i = 0; i < text.size(); ++i) {
        EXPECT_EQ(wt.access(i), static_cast<uint8_t>(text[i])) << "Access mismatch at " << i;
    }

    // Rank
    // 'a' occurs at indices: 0, 3, 5, 7, 10
    EXPECT_EQ(wt.rank('a', 0), 0);
    EXPECT_EQ(wt.rank('a', 1), 1);
    EXPECT_EQ(wt.rank('a', 3), 1);
    EXPECT_EQ(wt.rank('a', 4), 2);
    EXPECT_EQ(wt.rank('a', 6), 3);
    EXPECT_EQ(wt.rank('a', 8), 4);
    EXPECT_EQ(wt.rank('a', 11), 5);

    // 'b' occurs at indices: 1, 8
    EXPECT_EQ(wt.rank('b', 1), 0);
    EXPECT_EQ(wt.rank('b', 2), 1);
    EXPECT_EQ(wt.rank('b', 8), 1);
    EXPECT_EQ(wt.rank('b', 9), 2);

    // Select
    EXPECT_EQ(wt.select('a', 1), 0);
    EXPECT_EQ(wt.select('a', 2), 3);
    EXPECT_EQ(wt.select('a', 3), 5);
    EXPECT_EQ(wt.select('a', 4), 7);
    EXPECT_EQ(wt.select('a', 5), 10);
    EXPECT_EQ(wt.select('a', 6), 11);  // Out of range

    EXPECT_EQ(wt.select('b', 1), 1);
    EXPECT_EQ(wt.select('b', 2), 8);
    EXPECT_EQ(wt.select('b', 3), 11);

    // Character not present
    EXPECT_EQ(wt.rank('z', 11), 0);
    EXPECT_EQ(wt.select('z', 1), 11);
}

TEST(WaveletTreeTest, SerializationRoundtrip) {
    std::string text = "the quick brown fox jumps over the lazy dog 1234567890!@#$%^&*()";
    WaveletTree wt(text);

    std::stringstream ss;
    wt.serialize(ss);

    WaveletTree restored = WaveletTree::deserialize(ss);
    EXPECT_EQ(restored.size(), wt.size());

    for (size_t i = 0; i < text.size(); ++i) {
        EXPECT_EQ(restored.access(i), wt.access(i));
        uint8_t c = static_cast<uint8_t>(text[i]);
        EXPECT_EQ(restored.rank(c, i + 1), wt.rank(c, i + 1));
    }
}
