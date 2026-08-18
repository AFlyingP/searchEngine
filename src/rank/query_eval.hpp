#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "invidx/postings.hpp"
#include "rank/bm25.hpp"
#include "rank/wand.hpp"
#include "store/index_file.hpp"
#include "util/analyzer.hpp"

namespace needlefish {

enum class QueryType {
    Disjunction,  // Standard OR / scored BM25
    Conjunction,  // Exact AND (galloping)
    Phrase        // Exact phrase (positional)
};

struct QueryClause {
    std::string raw_term{};
    std::string normalized_term{};
    bool is_negated{false};
};

struct QueryResult {
    std::vector<SearchHit> hits{};
    size_t total_estimate{0};
    uint64_t took_us{0};
};

class QueryEvaluator {
  public:
    explicit QueryEvaluator(const IndexView& index, BM25Scorer scorer = BM25Scorer{});

    /**
     * @brief Execute a scored BM25 term disjunction query using Block-Max WAND.
     */
    [[nodiscard]] QueryResult search_disjunction(std::span<const std::string> terms, size_t k = 10,
                                                 bool use_wand = true) const;

    /**
     * @brief Execute a boolean AND conjunction with galloping / skip pointers.
     */
    [[nodiscard]] QueryResult search_conjunction(std::span<const std::string> terms,
                                                 size_t k = 10) const;

    /**
     * @brief Execute an exact positional phrase query.
     */
    [[nodiscard]] QueryResult search_phrase(std::span<const std::string> terms,
                                            size_t k = 10) const;

    /**
     * @brief Simple string query parser and evaluator.
     * Supports:
     *   - bare words: "information retrieval"
     *   - quoted phrase: "\"suffix array\""
     *   - negation: "index -lucene"
     */
    [[nodiscard]] QueryResult search(std::string_view query_str, size_t k = 10,
                                     bool use_wand = true) const;

  private:
    const IndexView& index_;
    BM25Scorer scorer_{};
    Analyzer analyzer_{true, true};
};

}  // namespace needlefish
