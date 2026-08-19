#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "automata/levenshtein.hpp"
#include "invidx/radix_trie.hpp"
#include "store/index_file.hpp"
#include "util/analyzer.hpp"

namespace needlefish {

struct Suggestion {
    std::string text{};
    size_t doc_freq{0};
    size_t edit_distance{0};
    float score{0.0f};

    bool operator>(const Suggestion& other) const noexcept {
        if (edit_distance != other.edit_distance)
            return edit_distance < other.edit_distance;
        if (doc_freq != other.doc_freq)
            return doc_freq > other.doc_freq;
        return text < other.text;
    }
};

/**
 * @brief Search-As-You-Type and Typo Correction Suggestion Engine.
 */
class AutocompleteEngine {
  public:
    explicit AutocompleteEngine(const IndexView& index);

    /**
     * @brief Prefix completion suggestions sorted by term frequency.
     */
    [[nodiscard]] std::vector<Suggestion> prefix_suggest(std::string_view prefix,
                                                         size_t max_results = 10) const;

    /**
     * @brief Typo-tolerant suggestions via Levenshtein automaton.
     */
    [[nodiscard]] std::vector<Suggestion> fuzzy_suggest(std::string_view word,
                                                        size_t max_distance = 2,
                                                        size_t max_results = 10) const;

    /**
     * @brief "Did you mean?" spelling correction for query terms.
     */
    [[nodiscard]] std::string did_you_mean(std::string_view query) const;

  private:
    const IndexView& index_;
    Analyzer analyzer_{false, false};  // No stemming for raw suggestions
};

}  // namespace needlefish
