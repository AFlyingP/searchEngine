#pragma once

#include <cstdint>
#include <queue>
#include <vector>

#include "invidx/postings.hpp"
#include "rank/bm25.hpp"
#include "store/index_file.hpp"

namespace needlefish {

struct SearchHit {
    uint32_t doc_id{0};
    float score{0.0f};

    bool operator>(const SearchHit& other) const noexcept {
        if (score != other.score)
            return score > other.score;
        return doc_id < other.doc_id;
    }
    bool operator<(const SearchHit& other) const noexcept {
        if (score != other.score)
            return score < other.score;
        return doc_id > other.doc_id;
    }
};

struct ScoredTermReader {
    PostingListReader reader{};
    double idf{0.0};
    float term_max_score{0.0f};

    [[nodiscard]] bool valid() const noexcept { return reader.valid(); }
    [[nodiscard]] uint32_t doc_id() const noexcept { return reader.doc_id(); }
};

/**
 * @brief Block-Max WAND dynamic pruning query evaluator for top-k scored disjunctions.
 */
class BlockMaxWAND {
  public:
    static std::vector<SearchHit> top_k_disjunction(std::vector<ScoredTermReader>& readers,
                                                    const IndexView& index, size_t k,
                                                    const BM25Scorer& scorer);

    static std::vector<SearchHit> top_k_naive_disjunction(std::vector<ScoredTermReader>& readers,
                                                          const IndexView& index, size_t k,
                                                          const BM25Scorer& scorer);
};

}  // namespace needlefish
