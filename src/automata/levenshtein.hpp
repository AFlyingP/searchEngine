#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "invidx/radix_trie.hpp"

namespace needlefish {

struct FuzzyMatch {
    std::string term{};
    size_t distance{0};
    TermPayload payload{};
};

/**
 * @brief Levenshtein DP-Row Automaton.
 * Operates at edit distance k in {1, 2} and directly intersects with the contiguous RadixTrie.
 */
class LevenshteinAutomaton {
  public:
    LevenshteinAutomaton(std::string_view target, size_t max_distance);

    /**
     * @brief State representation as a vector of prefix edit distances.
     */
    using State = std::vector<uint8_t>;

    [[nodiscard]] State initial_state() const;
    [[nodiscard]] State step(const State& state, char c) const;
    [[nodiscard]] bool is_accept(const State& state) const noexcept;
    [[nodiscard]] bool can_match(const State& state) const noexcept;
    [[nodiscard]] size_t distance(const State& state) const noexcept;

    /**
     * @brief Intersects the Levenshtein automaton with a RadixTrie in lockstep.
     * Prunes dead subtrees and finds all dictionary terms within max_distance.
     */
    [[nodiscard]] std::vector<FuzzyMatch> match_trie(const RadixTrie& trie,
                                                     size_t max_results = 50) const;

    [[nodiscard]] std::string_view target() const noexcept { return target_; }
    [[nodiscard]] size_t max_distance() const noexcept { return max_distance_; }

  private:
    void dfs_trie(uint32_t node_idx, const State& state, std::string& current_prefix,
                  const RadixTrie& trie, std::vector<FuzzyMatch>& matches,
                  size_t max_results) const;

    std::string target_;
    size_t max_distance_{1};
};

}  // namespace needlefish
