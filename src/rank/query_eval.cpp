#include "rank/query_eval.hpp"

#include <chrono>
#include <iostream>
#include <queue>
#include <sstream>

namespace needlefish {

QueryEvaluator::QueryEvaluator(const IndexView& index, BM25Scorer scorer)
    : index_(index), scorer_(scorer) {}

QueryResult QueryEvaluator::search_disjunction(std::span<const std::string> terms, size_t k,
                                               bool use_wand) const {
    const auto start_time = std::chrono::high_resolution_clock::now();

    if (terms.empty() || k == 0 || index_.total_docs() == 0) {
        return QueryResult{.hits = {}, .total_estimate = 0, .took_us = 0};
    }

    std::vector<ScoredTermReader> readers;
    for (const auto& raw_t : terms) {
        auto payload = index_.term_dict().lookup(raw_t);
        if (!payload.valid()) {
            std::string term = analyzer_.normalize_term(raw_t);
            payload = index_.term_dict().lookup(term);
        }
        if (payload.valid()) {
            const double idf = BM25Scorer::compute_idf(payload.doc_freq, index_.total_docs());
            readers.push_back(ScoredTermReader{.reader = index_.get_posting_reader(payload),
                                               .idf = idf,
                                               .term_max_score = payload.max_term_score});
        }
    }

    if (readers.empty()) {
        const auto end_time = std::chrono::high_resolution_clock::now();
        const uint64_t took = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());
        return QueryResult{.hits = {}, .total_estimate = 0, .took_us = took};
    }

    std::vector<SearchHit> hits;
    if (use_wand) {
        hits = BlockMaxWAND::top_k_disjunction(readers, index_, k, scorer_);
    } else {
        hits = BlockMaxWAND::top_k_naive_disjunction(readers, index_, k, scorer_);
    }

    const auto end_time = std::chrono::high_resolution_clock::now();
    const uint64_t took = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());

    return QueryResult{.hits = std::move(hits), .total_estimate = hits.size(), .took_us = took};
}

QueryResult QueryEvaluator::search_conjunction(std::span<const std::string> terms, size_t k) const {
    const auto start_time = std::chrono::high_resolution_clock::now();

    if (terms.empty() || k == 0 || index_.total_docs() == 0) {
        return QueryResult{.hits = {}, .total_estimate = 0, .took_us = 0};
    }

    std::vector<ScoredTermReader> readers;
    for (const auto& raw_t : terms) {
        auto payload = index_.term_dict().lookup(raw_t);
        if (!payload.valid()) {
            std::string term = analyzer_.normalize_term(raw_t);
            payload = index_.term_dict().lookup(term);
        }
        if (!payload.valid()) {
            // One term missing -> conjunction intersection is empty
            const auto end_time = std::chrono::high_resolution_clock::now();
            const uint64_t took = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time)
                    .count());
            return QueryResult{.hits = {}, .total_estimate = 0, .took_us = took};
        }
        const double idf = BM25Scorer::compute_idf(payload.doc_freq, index_.total_docs());
        readers.push_back(ScoredTermReader{.reader = index_.get_posting_reader(payload),
                                           .idf = idf,
                                           .term_max_score = payload.max_term_score});
    }

    // Sort by doc_freq ascending (shortest posting list first for galloping)
    std::sort(readers.begin(), readers.end(),
              [](const ScoredTermReader& a, const ScoredTermReader& b) {
                  return a.reader.total_docs() < b.reader.total_docs();
              });

    auto cmp = [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; };
    std::priority_queue<SearchHit, std::vector<SearchHit>, decltype(cmp)> heap(cmp);

    const double avg_doc_len = index_.avg_doc_len();

    bool finished = false;
    while (readers[0].valid() && !finished) {
        uint32_t candidate_doc = readers[0].doc_id();
        bool all_match = true;

        for (size_t i = 1; i < readers.size(); ++i) {
            readers[i].reader.advance(candidate_doc);
            if (!readers[i].valid()) {
                all_match = false;
                finished = true;
                break;
            }
            if (readers[i].doc_id() > candidate_doc) {
                readers[0].reader.advance(readers[i].doc_id());
                all_match = false;
                break;
            }
        }

        if (all_match) {
            // All readers match candidate_doc
            const uint32_t doc_len = index_.doc_metadata(candidate_doc).token_count;
            float doc_score = 0.0f;
            for (auto& r : readers) {
                doc_score += scorer_.score(r.reader.freq(), doc_len, avg_doc_len, r.idf);
                r.reader.next();
            }

            heap.push(SearchHit{.doc_id = candidate_doc, .score = doc_score});
            if (heap.size() > k) {
                heap.pop();
            }
        } else if (!finished && readers[0].valid() && readers[0].doc_id() == candidate_doc) {
            readers[0].reader.next();
        }
    }

    std::vector<SearchHit> hits;
    hits.reserve(heap.size());
    while (!heap.empty()) {
        hits.push_back(heap.top());
        heap.pop();
    }
    std::reverse(hits.begin(), hits.end());

    const auto end_time = std::chrono::high_resolution_clock::now();
    const uint64_t took = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());

    return QueryResult{.hits = std::move(hits), .total_estimate = hits.size(), .took_us = took};
}

QueryResult QueryEvaluator::search_phrase(std::span<const std::string> terms, size_t k) const {
    const auto start_time = std::chrono::high_resolution_clock::now();

    if (terms.empty() || k == 0 || index_.total_docs() == 0) {
        return QueryResult{.hits = {}, .total_estimate = 0, .took_us = 0};
    }

    std::vector<ScoredTermReader> readers;
    for (const auto& raw_t : terms) {
        std::string term = analyzer_.normalize_term(raw_t);
        const auto payload = index_.term_dict().lookup(term);
        if (!payload.valid()) {
            const auto end_time = std::chrono::high_resolution_clock::now();
            const uint64_t took = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time)
                    .count());
            return QueryResult{.hits = {}, .total_estimate = 0, .took_us = took};
        }
        const double idf = BM25Scorer::compute_idf(payload.doc_freq, index_.total_docs());
        readers.push_back(ScoredTermReader{.reader = index_.get_posting_reader(payload),
                                           .idf = idf,
                                           .term_max_score = payload.max_term_score});
    }

    auto cmp = [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.doc_id < b.doc_id;
    };
    std::priority_queue<SearchHit, std::vector<SearchHit>, decltype(cmp)> heap(cmp);

    const double avg_doc_len = index_.avg_doc_len();

    // Iterate across candidate matching docs
    bool finished = false;
    while (readers[0].valid() && !finished) {
        uint32_t candidate_doc = readers[0].doc_id();
        bool all_match = true;

        for (size_t i = 1; i < readers.size(); ++i) {
            readers[i].reader.advance(candidate_doc);
            if (!readers[i].valid()) {
                all_match = false;
                finished = true;
                break;
            }
            if (readers[i].doc_id() > candidate_doc) {
                readers[0].reader.advance(readers[i].doc_id());
                all_match = false;
                break;
            }
        }

        if (all_match) {
            // Read positions for all terms
            std::vector<std::vector<uint32_t>> term_positions(readers.size());
            for (size_t i = 0; i < readers.size(); ++i) {
                readers[i].reader.read_positions(term_positions[i]);
            }

            // Check for consecutive phrase positions: pos[i+1] == pos[i] + 1
            uint32_t phrase_matches = 0;
            const auto& first_positions = term_positions[0];

            for (uint32_t p0 : first_positions) {
                bool phrase_matched = true;
                for (size_t i = 1; i < term_positions.size(); ++i) {
                    const uint32_t target_pos = p0 + static_cast<uint32_t>(i);
                    const auto& pos_list = term_positions[i];
                    if (!std::binary_search(pos_list.begin(), pos_list.end(), target_pos)) {
                        phrase_matched = false;
                        break;
                    }
                }
                if (phrase_matched) {
                    phrase_matches++;
                }
            }

            if (phrase_matches > 0) {
                const uint32_t doc_len = index_.doc_metadata(candidate_doc).token_count;
                float phrase_score = 0.0f;
                for (const auto& r : readers) {
                    phrase_score += scorer_.score(phrase_matches, doc_len, avg_doc_len, r.idf);
                }

                heap.push(SearchHit{.doc_id = candidate_doc, .score = phrase_score});
                if (heap.size() > k) {
                    heap.pop();
                }
            }

            for (auto& r : readers) {
                r.reader.next();
            }
        } else if (!finished && readers[0].valid() && readers[0].doc_id() == candidate_doc) {
            readers[0].reader.next();
        }
    }

    std::vector<SearchHit> hits;
    hits.reserve(heap.size());
    while (!heap.empty()) {
        hits.push_back(heap.top());
        heap.pop();
    }
    std::reverse(hits.begin(), hits.end());

    const auto end_time = std::chrono::high_resolution_clock::now();
    const uint64_t took = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());

    return QueryResult{.hits = std::move(hits), .total_estimate = hits.size(), .took_us = took};
}

QueryResult QueryEvaluator::search(std::string_view query_str, size_t k, bool use_wand) const {
    if (query_str.empty()) {
        return QueryResult{};
    }

    // Check if phrase query: starts and ends with '"'
    if (query_str.size() >= 2 && query_str.front() == '"' && query_str.back() == '"') {
        std::string_view inner = query_str.substr(1, query_str.size() - 2);
        auto tokens = analyzer_.analyze(inner);
        std::vector<std::string> terms;
        for (const auto& tok : tokens) {
            terms.push_back(tok.term);
        }
        return search_phrase(terms, k);
    }

    // Split words to detect negated terms ("-term") and "OR" operator
    std::vector<std::string> positive_terms;
    std::vector<std::string> negated_terms;
    bool has_or = false;

    std::string q_str(query_str);
    std::istringstream iss(q_str);
    std::string token;
    while (iss >> token) {
        if (token == "OR" || token == "or") {
            has_or = true;
            continue;
        }
        if (token.size() > 1 && token.front() == '-') {
            auto neg_toks = analyzer_.analyze(token.substr(1));
            for (const auto& t : neg_toks) {
                negated_terms.push_back(t.term);
            }
        } else {
            auto pos_toks = analyzer_.analyze(token);
            for (const auto& t : pos_toks) {
                positive_terms.push_back(t.term);
            }
        }
    }

    if (positive_terms.empty()) {
        return QueryResult{};
    }

    QueryResult base_res;
    if (has_or || use_wand) {
        base_res = search_disjunction(positive_terms, k + negated_terms.size() * 5, use_wand);
    } else {
        base_res = search_conjunction(positive_terms, k + negated_terms.size() * 5);
    }

    if (negated_terms.empty() || base_res.hits.empty()) {
        if (base_res.hits.size() > k) {
            base_res.hits.resize(k);
        }
        return base_res;
    }

    // Filter out hits matching negated terms
    std::vector<PostingListReader> neg_readers;
    for (const auto& nt : negated_terms) {
        const auto payload = index_.term_dict().lookup(nt);
        if (payload.valid()) {
            neg_readers.push_back(index_.get_posting_reader(payload));
        }
    }

    if (neg_readers.empty()) {
        if (base_res.hits.size() > k) {
            base_res.hits.resize(k);
        }
        return base_res;
    }

    std::vector<SearchHit> filtered_hits;
    for (const auto& hit : base_res.hits) {
        bool matches_neg = false;
        for (auto& nr : neg_readers) {
            nr.advance(hit.doc_id);
            if (nr.valid() && nr.doc_id() == hit.doc_id) {
                matches_neg = true;
                break;
            }
        }
        if (!matches_neg) {
            filtered_hits.push_back(hit);
            if (filtered_hits.size() == k) break;
        }
    }

    base_res.hits = std::move(filtered_hits);
    base_res.total_estimate = base_res.hits.size();
    return base_res;
}

}  // namespace needlefish
