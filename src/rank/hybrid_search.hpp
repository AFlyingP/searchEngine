#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "automata/autocomplete.hpp"
#include "automata/levenshtein.hpp"
#include "automata/regex.hpp"
#include "rank/query_eval.hpp"
#include "store/index_file.hpp"

namespace needlefish {

enum class HybridQueryType { Standard, Fuzzy, Regex, Substring };

struct HybridSearchResult {
    std::vector<SearchHit> hits{};
    size_t took_us{0};
    HybridQueryType query_type{HybridQueryType::Standard};
    std::string correction_suggestion{};
};

/**
 * @brief Hybrid Search Engine combining Inverted Index (BM25, WAND),
 * Levenshtein Automata, Regex Engine, and FM-Index Substring search.
 */
class HybridSearchEngine {
  public:
    explicit HybridSearchEngine(const IndexView& index);

    /**
     * @brief Intelligent unified search router.
     */
    [[nodiscard]] HybridSearchResult search(std::string_view query_str, size_t top_k = 10,
                                            size_t max_fuzzy_distance = 0);

    /**
     * @brief Fuzzy BM25 search via Levenshtein term expansion on the Radix Trie.
     */
    [[nodiscard]] HybridSearchResult search_fuzzy(std::string_view query_str,
                                                  size_t max_distance = 2, size_t top_k = 10);

    /**
     * @brief Exact substring search across documents using the FM-Index.
     */
    [[nodiscard]] HybridSearchResult search_substring(std::string_view substring,
                                                      size_t top_k = 10);

    /**
     * @brief Regex search across stored document texts.
     */
    [[nodiscard]] HybridSearchResult search_regex(std::string_view regex_pattern,
                                                  size_t top_k = 10);

    [[nodiscard]] const IndexView& index() const noexcept { return index_; }
    [[nodiscard]] const AutocompleteEngine& autocomplete() const noexcept { return autocomplete_; }

  private:
    [[nodiscard]] uint32_t find_doc_for_offset(uint64_t text_offset) const noexcept;

    const IndexView& index_;
    QueryEvaluator query_eval_;
    AutocompleteEngine autocomplete_;
    Analyzer analyzer_{true, true};
};

}  // namespace needlefish
