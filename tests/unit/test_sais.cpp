#include <gtest/gtest.h>
#include "sa/sais.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace needlefish;

TEST(SaisTest, ClassicMississippi) {
    // "mmiissiissiippii"
    std::string text = "mmiissiissiippii";
    auto sa = build_suffix_array(text);
    ASSERT_EQ(sa.size(), 16);

    // Verify suffixes are strictly ascending lexicographically
    for (size_t i = 1; i < sa.size(); ++i) {
        std::string_view prev(text.data() + sa[i - 1], text.size() - sa[i - 1]);
        std::string_view curr(text.data() + sa[i], text.size() - sa[i]);
        EXPECT_LT(prev, curr) << "Suffix at " << i - 1 << " (" << prev << ") not less than suffix at "
                              << i << " (" << curr << ")";
    }
}

TEST(SaisTest, AdversarialRepetitive) {
    // All equal
    std::string all_a(200, 'a');
    auto sa_a = build_suffix_array(all_a);
    ASSERT_EQ(sa_a.size(), 200);
    for (size_t i = 0; i < 200; ++i) {
        EXPECT_EQ(sa_a[i], 199 - i);
    }

    // Periodic "abababab..."
    std::string ab;
    for (int i = 0; i < 50; ++i) ab += "ab";
    auto sa_ab = build_suffix_array(ab);
    ASSERT_EQ(sa_ab.size(), 100);

    for (size_t i = 1; i < sa_ab.size(); ++i) {
        std::string_view prev(ab.data() + sa_ab[i - 1], ab.size() - sa_ab[i - 1]);
        std::string_view curr(ab.data() + sa_ab[i], ab.size() - sa_ab[i]);
        EXPECT_LT(prev, curr);
    }
}

TEST(SaisTest, DeBruijnSequence) {
    // Binary de Bruijn sequence B(2, 6) of length 64
    std::string db = "0000001000011000101000111001001011001101001111010101110110111111";
    auto sa_db = build_suffix_array(db);
    ASSERT_EQ(sa_db.size(), 64);

    for (size_t i = 1; i < sa_db.size(); ++i) {
        std::string_view prev(db.data() + sa_db[i - 1], db.size() - sa_db[i - 1]);
        std::string_view curr(db.data() + sa_db[i], db.size() - sa_db[i]);
        EXPECT_LT(prev, curr);
    }
}
