#include "automata/levenshtein.hpp"
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// Reference Dynamic Programming edit distance
static size_t dp_edit_distance(std::string_view s1, std::string_view s2) {
    const size_t m = s1.size();
    const size_t n = s2.size();
    std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1, 0));

    for (size_t i = 0; i <= m; ++i) dp[i][0] = i;
    for (size_t j = 0; j <= n; ++j) dp[0][j] = j;

    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            if (s1[i - 1] == s2[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + std::min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
            }
        }
    }
    return dp[m][n];
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2 || size > 256) {
        return 0;
    }

    size_t mid = size / 2;
    std::string query(reinterpret_cast<const char*>(data), mid);
    std::string target(reinterpret_cast<const char*>(data + mid), size - mid);

    for (size_t k : {1ULL, 2ULL}) {
        needlefish::LevenshteinAutomaton dfa(query, k);
        auto state = dfa.initial_state();

        for (char c : target) {
            state = dfa.step(state, c);
            if (!dfa.can_match(state)) {
                break;
            }
        }

        bool dfa_accepts = dfa.is_accept(state);
        size_t dp_dist = dp_edit_distance(query, target);
        bool dp_accepts = (dp_dist <= k);

        if (dfa_accepts != dp_accepts) {
            std::abort();
        }
    }

    return 0;
}

#ifndef NEEDLEFISH_LIBFUZZER
int main() {
    std::vector<std::pair<std::string, std::string>> test_cases = {
        {"algorithm", "algotithm"},
        {"search", "serch"},
        {"needlefish", "needlepig"},
        {"a", "ab"},
        {"", "abc"},
        {"test", "test"}
    };

    for (const auto& [q, t] : test_cases) {
        std::string combined = q + t;
        LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(combined.data()), combined.size());
    }

    return 0;
}
#endif
