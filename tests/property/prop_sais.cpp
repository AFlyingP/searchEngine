#include <gtest/gtest.h>
#include "sa/sais.hpp"

#include <algorithm>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace needlefish;

namespace {

// Oracle 1: Naive std::sort of suffixes O(n^2 log n)
std::vector<size_t> naive_suffix_sort(std::string_view text) {
    const size_t n = text.size();
    std::vector<size_t> sa(n);
    std::iota(sa.begin(), sa.end(), 0);

    std::sort(sa.begin(), sa.end(), [&](size_t a, size_t b) {
        return std::string_view(text.data() + a, n - a) <
               std::string_view(text.data() + b, n - b);
    });

    return sa;
}

// Oracle 2: Prefix Doubling (Larsson-Sadakane style O(n log^2 n))
std::vector<size_t> doubling_suffix_sort(std::string_view text) {
    const size_t n = text.size();
    if (n == 0) return {};

    std::vector<size_t> sa(n);
    std::iota(sa.begin(), sa.end(), 0);

    std::vector<int64_t> rank(n);
    for (size_t i = 0; i < n; ++i) {
        rank[i] = static_cast<unsigned char>(text[i]);
    }

    std::vector<int64_t> tmp_rank(n);

    for (size_t k = 1; k < n; k *= 2) {
        auto cmp = [&](size_t i, size_t j) {
            if (rank[i] != rank[j]) return rank[i] < rank[j];
            int64_t ri = (i + k < n) ? rank[i + k] : -1;
            int64_t rj = (j + k < n) ? rank[j + k] : -1;
            return ri < rj;
        };

        std::sort(sa.begin(), sa.end(), cmp);

        tmp_rank[sa[0]] = 0;
        for (size_t i = 1; i < n; ++i) {
            tmp_rank[sa[i]] = tmp_rank[sa[i - 1]] + (cmp(sa[i - 1], sa[i]) ? 1 : 0);
        }
        rank = tmp_rank;
        if (rank[sa.back()] == static_cast<int64_t>(n - 1)) {
            break;
        }
    }

    return sa;
}

}  // namespace

TEST(SaisPropertyTest, CompareAgainstNaiveOracle) {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> char_dist('a', 'd');  // Small alphabet for max collisions

    for (int trial = 0; trial < 100; ++trial) {
        const size_t len = 500 + trial * 50;  // Up to ~5500
        std::string text(len, '\0');
        for (size_t i = 0; i < len; ++i) {
            text[i] = static_cast<char>(char_dist(rng));
        }

        auto sa_sais = build_suffix_array(text);
        auto sa_naive = naive_suffix_sort(text);

        ASSERT_EQ(sa_sais.size(), sa_naive.size());
        for (size_t i = 0; i < len; ++i) {
            ASSERT_EQ(sa_sais[i], sa_naive[i]) << "Mismatch at index " << i << " in trial " << trial;
        }
    }
}

TEST(SaisPropertyTest, CompareAgainstDoublingOracleLarge) {
    std::mt19937_64 rng(101);
    std::uniform_int_distribution<int> char_dist(1, 255);

    // Large test on 100k string
    const size_t len = 100000;
    std::string text(len, '\0');
    for (size_t i = 0; i < len; ++i) {
        text[i] = static_cast<char>(char_dist(rng));
    }

    SaisStats stats;
    auto sa_sais = build_suffix_array(text, &stats);
    auto sa_doubling = doubling_suffix_sort(text);

    ASSERT_EQ(sa_sais.size(), sa_doubling.size());
    for (size_t i = 0; i < len; ++i) {
        ASSERT_EQ(sa_sais[i], sa_doubling[i]) << "Large mismatch at index " << i;
    }
}
