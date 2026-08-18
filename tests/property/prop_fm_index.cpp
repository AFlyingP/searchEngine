#include <gtest/gtest.h>
#include "fm/fm_index.hpp"

#include <algorithm>
#include <random>
#include <string>
#include <vector>

using namespace needlefish;

namespace {

std::vector<size_t> brute_force_locate(std::string_view text, std::string_view pattern) {
    if (pattern.empty()) return {};
    std::vector<size_t> positions;
    size_t pos = text.find(pattern, 0);
    while (pos != std::string_view::npos) {
        positions.push_back(pos);
        pos = text.find(pattern, pos + 1);
    }
    return positions;
}

}  // namespace

TEST(FMIndexPropertyTest, TenThousandRandomQueries) {
    std::mt19937_64 rng(777);
    std::uniform_int_distribution<int> char_dist('a', 'f');  // 6 char alphabet to ensure ample substrings

    const size_t text_len = 10000;
    std::string text(text_len, '\0');
    for (size_t i = 0; i < text_len; ++i) {
        text[i] = static_cast<char>(char_dist(rng));
    }

    FMIndex fmi(text, 8);

    std::uniform_int_distribution<size_t> start_dist(0, text_len - 1);
    std::uniform_int_distribution<size_t> len_dist(1, 15);
    std::uniform_int_distribution<int> pattern_type_dist(0, 5);

    const size_t num_queries = 10000;
    for (size_t q = 0; q < num_queries; ++q) {
        std::string pattern;
        int p_type = pattern_type_dist(rng);

        if (p_type == 0) {
            // Substring from text (guaranteed hit)
            size_t s = start_dist(rng);
            size_t l = std::min(len_dist(rng), text_len - s);
            pattern = text.substr(s, l);
        } else if (p_type == 1) {
            // Single char
            pattern = std::string(1, static_cast<char>(char_dist(rng)));
        } else if (p_type == 2) {
            // Random pattern (may or may not exist)
            size_t l = len_dist(rng);
            pattern.resize(l);
            for (size_t i = 0; i < l; ++i) pattern[i] = static_cast<char>(char_dist(rng));
        } else if (p_type == 3) {
            // Pattern with foreign character (guaranteed 0 matches)
            pattern = "xyz";
        } else if (p_type == 4) {
            // Whole text prefix
            pattern = text.substr(0, std::min<size_t>(50, text_len));
        } else {
            // Whole text
            pattern = text;
        }

        auto expected_positions = brute_force_locate(text, pattern);
        size_t expected_count = expected_positions.size();

        size_t actual_count = fmi.count(pattern);
        ASSERT_EQ(actual_count, expected_count)
            << "Count mismatch for pattern '" << pattern << "' in query " << q;

        auto actual_positions = fmi.locate(pattern);
        ASSERT_EQ(actual_positions, expected_positions)
            << "Locate mismatch for pattern '" << pattern << "' in query " << q;
    }
}
