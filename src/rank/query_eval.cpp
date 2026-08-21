#include "rank/query_eval.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace needlefish {

QueryEvaluator::QueryEvaluator(const IndexView& index, BM25Scorer scorer)
    : index_(index), scorer_(scorer) {
    if (index_.total_docs() > 0) {
        const auto& st = index_.stats();
        if (st.k1 > 0.0f && std::abs(st.k1 - scorer_.k1()) > 1e-4f) {
            throw std::invalid_argument("Scorer k1 mismatch with index stats");
        }
        if (st.b > 0.0f && std::abs(st.b - scorer_.b()) > 1e-4f) {
            throw std::invalid_argument("Scorer b mismatch with index stats");
        }
    }
}

QueryResult QueryEvaluator::search_disjunction(std::span<const std::string> terms, size_t k,
                                               bool use_wand) const {
    const auto start_time = std::chrono::high_resolution_clock::now();

    if (terms.empty() || k == 0 || index_.total_docs() == 0) {
        return QueryResult{.hits = {}, .total_estimate = 0, .took_us = 0};
    }

    // Deduplicate input terms
    std::vector<std::string> unique_terms;
    std::unordered_set<std::string> seen;
    for (const auto& t : terms) {
        if (seen.insert(t).second) {
            unique_terms.push_back(t);
        }
    }

    std::vector<ScoredTermReader> readers;
    for (const auto& raw_t : unique_terms) {
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

    // Deduplicate input terms
    std::vector<std::string> unique_terms;
    std::unordered_set<std::string> seen;
    for (const auto& t : terms) {
        if (seen.insert(t).second) {
            unique_terms.push_back(t);
        }
    }

    std::vector<ScoredTermReader> readers;
    for (const auto& raw_t : unique_terms) {
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

    auto cmp = [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.doc_id < b.doc_id; // larger doc_id popped first -> smaller doc_id kept
    };
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
    std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.doc_id < b.doc_id;
    });

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
        auto payload = index_.term_dict().lookup(raw_t);
        if (!payload.valid()) {
            std::string term = analyzer_.normalize_term(raw_t);
            payload = index_.term_dict().lookup(term);
        }
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
    std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.doc_id < b.doc_id;
    });

    const auto end_time = std::chrono::high_resolution_clock::now();
    const uint64_t took = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());

    return QueryResult{.hits = std::move(hits), .total_estimate = hits.size(), .took_us = took};
}

QueryResult QueryEvaluator::search(std::string_view query_str, size_t k, bool use_wand) const {
    if (query_str.empty()) {
        return QueryResult{};
    }

    // Check for quoted phrase queries (including phrase queries with negated terms like '"a b" -c')
    std::string q_str(query_str);
    size_t q_start = q_str.find('"');
    size_t q_end = (q_start != std::string::npos) ? q_str.find('"', q_start + 1) : std::string::npos;
    if (q_start != std::string::npos && q_end != std::string::npos) {
        std::string phrase_inner = q_str.substr(q_start + 1, q_end - q_start - 1);
        auto p_toks = analyzer_.analyze(phrase_inner);
        std::vector<std::string> phrase_terms;
        for (const auto& tok : p_toks) {
            phrase_terms.push_back(tok.term);
        }

        std::string remaining = q_str.substr(0, q_start) + " " + q_str.substr(q_end + 1);
        std::istringstream iss(remaining);
        std::string token;
        std::vector<std::string> negated_terms;
        while (iss >> token) {
            if (token.size() > 1 && token.front() == '-') {
                auto neg_toks = analyzer_.analyze(token.substr(1));
                for (const auto& t : neg_toks) {
                    negated_terms.push_back(t.term);
                }
            }
        }

        if (!phrase_terms.empty()) {
            if (negated_terms.empty()) {
                return search_phrase(phrase_terms, k);
            }
            auto res = search_phrase(phrase_terms, k + negated_terms.size() * 5);
            QueryResult filtered;
            filtered.total_estimate = res.total_estimate;

            std::vector<std::vector<uint32_t>> neg_doc_lists;
            for (const auto& neg : negated_terms) {
                auto neg_payload = index_.term_payload(neg);
                if (neg_payload.valid() && neg_payload.doc_freq > 0) {
                    PostingListReader r(index_.postings_section_data() + neg_payload.postings_offset,
                                        neg_payload.postings_len);
                    std::vector<uint32_t> neg_docs;
                    neg_docs.reserve(neg_payload.doc_freq);
                    while (r.has_next()) {
                        neg_docs.push_back(r.doc_id());
                        r.next();
                    }
                    neg_doc_lists.push_back(std::move(neg_docs));
                }
            }

            for (const auto& hit : res.hits) {
                bool excluded = false;
                for (const auto& ndocs : neg_doc_lists) {
                    if (std::binary_search(ndocs.begin(), ndocs.end(), hit.doc_id)) {
                        excluded = true;
                        break;
                    }
                }
                if (!excluded) {
                    filtered.hits.push_back(hit);
                    if (filtered.hits.size() >= k) break;
                }
            }
            return filtered;
        }
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

    // Deduplicate positive and negated terms
    std::vector<std::string> unique_pos;
    {
        std::unordered_set<std::string> seen;
        for (const auto& t : positive_terms) {
            if (seen.insert(t).second) {
                unique_pos.push_back(t);
            }
        }
    }
    std::vector<std::string> unique_neg;
    {
        std::unordered_set<std::string> seen;
        for (const auto& t : negated_terms) {
            if (seen.insert(t).second) {
                unique_neg.push_back(t);
            }
        }
    }

    // Decode full doc ID lists for all negated terms once (stateless, sorted)
    std::vector<std::vector<uint32_t>> neg_doc_lists;
    for (const auto& nt : unique_neg) {
        auto payload = index_.term_dict().lookup(nt);
        if (!payload.valid()) {
            std::string term = analyzer_.normalize_term(nt);
            payload = index_.term_dict().lookup(term);
        }
        if (payload.valid()) {
            auto r = index_.get_posting_reader(payload);
            std::vector<uint32_t> list;
            list.reserve(payload.doc_freq);
            while (r.valid()) {
                list.push_back(r.doc_id());
                r.next();
            }
            if (!list.empty()) {
                neg_doc_lists.push_back(std::move(list));
            }
        }
    }

    auto is_excluded = [&](uint32_t doc_id) -> bool {
        for (const auto& doc_list : neg_doc_lists) {
            if (std::binary_search(doc_list.begin(), doc_list.end(), doc_id)) {
                return true;
            }
        }
        return false;
    };

    // If no negated terms exist, execute standard search
    if (neg_doc_lists.empty()) {
        if (has_or) {
            return search_disjunction(unique_pos, k, use_wand);
        } else if (unique_pos.size() == 1) {
            return search_disjunction(unique_pos, k, use_wand);
        } else {
            return search_conjunction(unique_pos, k);
        }
    }

    // When negated terms exist, over-fetch candidate hits until k unexcluded hits are found
    size_t fetch_k = std::max(k * 2, k + 16);
    std::vector<SearchHit> final_hits;

    while (true) {
        QueryResult cand_res;
        if (has_or) {
            cand_res = search_disjunction(unique_pos, fetch_k, use_wand);
        } else if (unique_pos.size() == 1) {
            cand_res = search_disjunction(unique_pos, fetch_k, use_wand);
        } else {
            cand_res = search_conjunction(unique_pos, fetch_k);
        }

        final_hits.clear();
        for (const auto& hit : cand_res.hits) {
            if (!is_excluded(hit.doc_id)) {
                final_hits.push_back(hit);
                if (final_hits.size() == k) break;
            }
        }

        if (final_hits.size() >= k || cand_res.hits.size() < fetch_k || fetch_k >= index_.total_docs()) {
            cand_res.hits = std::move(final_hits);
            cand_res.total_estimate = cand_res.hits.size();
            return cand_res;
        }

        fetch_k = std::min(index_.total_docs(), fetch_k * 2);
    }
}

}  // namespace needlefish

