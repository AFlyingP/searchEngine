#include <gtest/gtest.h>
#include "wavelet/wavelet_tree.hpp"

#include <random>
#include <string>
#include <vector>

using namespace needlefish;

class WaveletOracle {
public:
    explicit WaveletOracle(std::string text) : text_(std::move(text)) {
        for (size_t i = 0; i < text_.size(); ++i) {
            uint8_t c = static_cast<uint8_t>(text_[i]);
            positions_[c].push_back(i);
        }
    }

    uint8_t access(size_t index) const {
        return static_cast<uint8_t>(text_[index]);
    }

    size_t rank(uint8_t c, size_t index) const {
        size_t count = 0;
        size_t bound = std::min(index, text_.size());
        for (size_t i = 0; i < bound; ++i) {
            if (static_cast<uint8_t>(text_[i]) == c) {
                count++;
            }
        }
        return count;
    }

    size_t select(uint8_t c, size_t k) const {
        if (k == 0 || k > positions_[c].size()) {
            return text_.size();
        }
        return positions_[c][k - 1];
    }

private:
    std::string text_;
    std::array<std::vector<size_t>, 256> positions_{};
};

TEST(WaveletPropertyTest, RandomTextDifferential) {
    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<int> char_dist(0, 255);

    const size_t text_len = 50000;
    std::string text(text_len, '\0');
    for (size_t i = 0; i < text_len; ++i) {
        text[i] = static_cast<char>(char_dist(rng));
    }

    WaveletTree wt(text);
    WaveletOracle oracle(text);

    std::uniform_int_distribution<size_t> idx_dist(0, text_len - 1);
    std::uniform_int_distribution<size_t> rank_idx_dist(0, text_len);
    std::uniform_int_distribution<int> query_char_dist(0, 255);
    std::uniform_int_distribution<int> op_dist(0, 2);

    const size_t num_queries = 200000;
    for (size_t q = 0; q < num_queries; ++q) {
        int op = op_dist(rng);
        if (op == 0) {
            size_t idx = idx_dist(rng);
            ASSERT_EQ(wt.access(idx), oracle.access(idx));
        } else if (op == 1) {
            uint8_t c = static_cast<uint8_t>(query_char_dist(rng));
            size_t idx = rank_idx_dist(rng);
            ASSERT_EQ(wt.rank(c, idx), oracle.rank(c, idx));
        } else {
            uint8_t c = static_cast<uint8_t>(query_char_dist(rng));
            size_t total_c = wt.count(c);
            if (total_c > 0) {
                std::uniform_int_distribution<size_t> k_dist(1, total_c);
                size_t k = k_dist(rng);
                ASSERT_EQ(wt.select(c, k), oracle.select(c, k));
            }
        }
    }
}
