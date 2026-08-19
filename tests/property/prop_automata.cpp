#include <algorithm>
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

#include "automata/levenshtein.hpp"
#include "automata/regex.hpp"

using namespace needlefish;

namespace {

// Classical Wagner-Fischer dynamic programming matrix oracle
size_t wagner_fischer_distance(std::string_view s1, std::string_view s2) {
    const size_t m = s1.size();
    const size_t n = s2.size();
    std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1, 0));

    for (size_t i = 0; i <= m; ++i)
        dp[i][0] = i;
    for (size_t j = 0; j <= n; ++j)
        dp[0][j] = j;

    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            const size_t cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1,        // deletion
                dp[i][j - 1] + 1,        // insertion
                dp[i - 1][j - 1] + cost  // substitution
            });
        }
    }
    return dp[m][n];
}

}  // namespace

TEST(AutomataPropertyTest, LevenshteinDfaVsWagnerFischerMatrixOracle5000Pairs) {
    std::mt19937 rng(42);
    const std::string alphabet = "abcdefgh";

    auto generate_random_word = [&](size_t len) {
        std::string w;
        w.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            w.push_back(alphabet[rng() % alphabet.size()]);
        }
        return w;
    };

    for (int trial = 0; trial < 5000; ++trial) {
        const size_t len1 = 3 + (rng() % 8);
        const size_t len2 = 3 + (rng() % 8);
        const std::string target = generate_random_word(len1);
        const std::string candidate = generate_random_word(len2);
        const size_t max_k = 1 + (rng() % 2);  // k in {1, 2}

        const size_t oracle_dist = wagner_fischer_distance(target, candidate);

        LevenshteinAutomaton dfa(target, max_k);
        auto state = dfa.initial_state();
        for (char c : candidate) {
            state = dfa.step(state, c);
        }

        const bool dfa_accept = dfa.is_accept(state);
        const size_t dfa_dist = dfa.distance(state);

        if (oracle_dist <= max_k) {
            EXPECT_TRUE(dfa_accept)
                << "DFA failed to accept for target=" << target << " cand=" << candidate
                << " oracle_dist=" << oracle_dist << " max_k=" << max_k;
            EXPECT_EQ(dfa_dist, oracle_dist)
                << "Distance mismatch for target=" << target << " cand=" << candidate;
        } else {
            EXPECT_FALSE(dfa_accept)
                << "DFA falsely accepted for target=" << target << " cand=" << candidate
                << " oracle_dist=" << oracle_dist << " max_k=" << max_k;
        }
    }
}
