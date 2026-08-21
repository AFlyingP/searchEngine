#include "rank/wand.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace needlefish {

std::vector<SearchHit> BlockMaxWAND::top_k_disjunction(std::vector<ScoredTermReader>& readers,
                                                       const IndexView& index, size_t k,
                                                       const BM25Scorer& scorer) {
    if (readers.empty() || k == 0) {
        return {};
    }

    // Min-heap storing top-k hits: smallest score on top, ties broken so higher doc_id pops first
    auto cmp = [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.doc_id < b.doc_id;
    };
    std::priority_queue<SearchHit, std::vector<SearchHit>, decltype(cmp)> heap(cmp);

    float threshold = 0.0f;
    const double avg_doc_len = index.avg_doc_len();

    // Filter out initially exhausted readers
    std::vector<ScoredTermReader*> active;
    active.reserve(readers.size());
    for (auto& r : readers) {
        if (r.valid()) {
            active.push_back(&r);
        }
    }

    while (!active.empty()) {
        // Sort active readers by current doc_id
        std::sort(active.begin(), active.end(),
                  [](const ScoredTermReader* a, const ScoredTermReader* b) {
                      return a->doc_id() < b->doc_id();
                  });

        // Find pivot
        float accumulated_max_score = 0.0f;
        size_t pivot_idx = active.size();

        for (size_t i = 0; i < active.size(); ++i) {
            accumulated_max_score += active[i]->reader.block_max_score();
            if (accumulated_max_score > threshold) {
                pivot_idx = i;
                break;
            }
        }

        if (pivot_idx == active.size()) {
            // Even if all remaining terms match at their max possible score in their current
            // blocks, they cannot beat the current threshold. Terminate early!
            break;
        }

        const uint32_t pivot_doc = active[pivot_idx]->doc_id();

        if (active[0]->doc_id() == pivot_doc) {
            // Evaluate full BM25 score for pivot_doc
            float doc_score = 0.0f;
            const uint32_t doc_len = index.doc_metadata(pivot_doc).token_count;

            for (auto* r : active) {
                if (r->doc_id() == pivot_doc) {
                    doc_score += scorer.score(r->reader.freq(), doc_len, avg_doc_len, r->idf);
                    r->reader.next();
                }
            }

            if (doc_score > threshold) {
                heap.push(SearchHit{.doc_id = pivot_doc, .score = doc_score});
                if (heap.size() > k) {
                    heap.pop();
                }
                if (heap.size() == k) {
                    threshold = heap.top().score;
                }
            }

            // Remove any readers that reached the end
            active.erase(std::remove_if(active.begin(), active.end(),
                                        [](const ScoredTermReader* r) { return !r->valid(); }),
                         active.end());
        } else {
            // Advanced skip: active[0] cannot reach threshold alone.
            // Advance active[0] to pivot_doc
            active[0]->reader.advance(pivot_doc);
            if (!active[0]->valid()) {
                active.erase(active.begin());
            }
        }
    }

    // Extract top-k from min-heap and sort deterministically
    std::vector<SearchHit> results;
    results.reserve(heap.size());
    while (!heap.empty()) {
        results.push_back(heap.top());
        heap.pop();
    }
    std::sort(results.begin(), results.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.doc_id < b.doc_id;
    });
    return results;
}

std::vector<SearchHit> BlockMaxWAND::top_k_naive_disjunction(std::vector<ScoredTermReader>& readers,
                                                             const IndexView& index, size_t k,
                                                             const BM25Scorer& scorer) {
    if (readers.empty() || k == 0) {
        return {};
    }

    std::unordered_map<uint32_t, float> doc_scores;
    doc_scores.reserve(65536);
    const double avg_doc_len = index.avg_doc_len();

    for (auto& r : readers) {
        while (r.valid()) {
            const uint32_t d = r.doc_id();
            const uint32_t doc_len = index.doc_metadata(d).token_count;
            doc_scores[d] += scorer.score(r.reader.freq(), doc_len, avg_doc_len, r.idf);
            r.reader.next();
        }
    }

    std::vector<SearchHit> all_hits;
    all_hits.reserve(doc_scores.size());
    for (const auto& [doc, score] : doc_scores) {
        all_hits.push_back(SearchHit{.doc_id = doc, .score = score});
    }

    std::sort(all_hits.begin(), all_hits.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.doc_id < b.doc_id;
    });

    if (all_hits.size() > k) {
        all_hits.resize(k);
    }
    return all_hits;
}

}  // namespace needlefish
