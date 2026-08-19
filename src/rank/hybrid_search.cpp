#include "rank/hybrid_search.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <unordered_map>

namespace needlefish {

HybridSearchEngine::HybridSearchEngine(const IndexView& index)
    : index_(index), query_eval_(index), autocomplete_(index) {}

uint32_t HybridSearchEngine::find_doc_for_offset(uint64_t text_offset) const noexcept {
    const size_t num_docs = index_.total_docs();
    if (num_docs == 0)
        return 0;

    // Binary search over doc_records_ by text_offset
    size_t lo = 0, hi = num_docs;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const auto& meta = index_.doc_metadata(static_cast<uint32_t>(mid));
        if (text_offset < meta.title_offset) {
            hi = mid;
        } else if (text_offset >= meta.text_offset + meta.text_len) {
            lo = mid + 1;
        } else {
            return static_cast<uint32_t>(mid);
        }
    }
    return static_cast<uint32_t>(std::min(lo, num_docs - 1));
}

HybridSearchResult HybridSearchEngine::search(std::string_view query_str, size_t top_k,
                                              size_t max_fuzzy_distance) {
    if (query_str.empty()) {
        return HybridSearchResult{};
    }

    // 1. Regex query: starts and ends with '/' (e.g. "/[a-z]+_pattern/")
    if (query_str.size() >= 2 && query_str.front() == '/' && query_str.back() == '/') {
        std::string_view regex_pat = query_str.substr(1, query_str.size() - 2);
        return search_regex(regex_pat, top_k);
    }

    // 2. Explicit fuzzy request
    if (max_fuzzy_distance > 0 || query_str.find('~') != std::string_view::npos) {
        size_t dist = (max_fuzzy_distance > 0) ? max_fuzzy_distance : 2;
        std::string clean_query(query_str);
        const size_t tilde_pos = clean_query.find('~');
        if (tilde_pos != std::string::npos) {
            if (tilde_pos + 1 < clean_query.size()) {
                try {
                    dist = static_cast<size_t>(std::stoul(clean_query.substr(tilde_pos + 1)));
                } catch (...) {
                    dist = 2;
                }
            }
            clean_query = clean_query.substr(0, tilde_pos);
        }
        return search_fuzzy(clean_query, dist, top_k);
    }

    // 3. Standard BM25 / Phrase search
    const auto t0 = std::chrono::high_resolution_clock::now();
    auto result = query_eval_.search(query_str, top_k);
    const auto t1 = std::chrono::high_resolution_clock::now();

    HybridSearchResult res{
        .hits = std::move(result.hits),
        .took_us = static_cast<size_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
        .query_type = HybridQueryType::Standard,
        .correction_suggestion = ""};

    // If 0 results were found, generate spelling correction
    if (res.hits.empty()) {
        res.correction_suggestion = autocomplete_.did_you_mean(query_str);
    }

    return res;
}

HybridSearchResult HybridSearchEngine::search_fuzzy(std::string_view query_str, size_t max_distance,
                                                    size_t top_k) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    auto tokens = analyzer_.analyze(query_str);
    if (tokens.empty()) {
        return HybridSearchResult{.took_us = 0, .query_type = HybridQueryType::Fuzzy};
    }

    std::vector<std::string> expanded_terms;
    std::set<std::string> seen_terms;

    for (const auto& tok : tokens) {
        LevenshteinAutomaton dfa(tok.term, max_distance);
        auto matches = dfa.match_trie(index_.term_dict(), 10);
        for (const auto& m : matches) {
            if (seen_terms.insert(m.term).second) {
                expanded_terms.push_back(m.term);
            }
        }
    }

    auto eval_res = query_eval_.search_disjunction(expanded_terms, top_k, /*use_wand=*/true);

    const auto t1 = std::chrono::high_resolution_clock::now();
    const size_t took =
        static_cast<size_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    return HybridSearchResult{.hits = std::move(eval_res.hits),
                              .took_us = took,
                              .query_type = HybridQueryType::Fuzzy,
                              .correction_suggestion = ""};
}

HybridSearchResult HybridSearchEngine::search_substring(std::string_view substring, size_t top_k) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    if (!index_.has_fm_index() || substring.empty()) {
        return HybridSearchResult{.took_us = 0, .query_type = HybridQueryType::Substring};
    }

    const auto* fmi = index_.fm_index();
    auto offsets = fmi->locate(substring);

    std::unordered_map<uint32_t, uint32_t> doc_match_counts;
    for (size_t offset : offsets) {
        uint32_t doc_id = find_doc_for_offset(offset);
        doc_match_counts[doc_id]++;
    }

    std::vector<SearchHit> hits;
    for (auto [doc_id, count] : doc_match_counts) {
        hits.push_back(SearchHit{.doc_id = doc_id, .score = static_cast<float>(count)});
    }

    std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.doc_id < b.doc_id;
    });

    if (hits.size() > top_k) {
        hits.resize(top_k);
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    const size_t took =
        static_cast<size_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    return HybridSearchResult{.hits = std::move(hits),
                              .took_us = took,
                              .query_type = HybridQueryType::Substring,
                              .correction_suggestion = ""};
}

HybridSearchResult HybridSearchEngine::search_regex(std::string_view regex_pattern, size_t top_k) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    Regex reg(regex_pattern);
    std::vector<SearchHit> hits;

    const size_t num_docs = index_.total_docs();
    for (uint32_t doc_id = 0; doc_id < num_docs; ++doc_id) {
        std::string_view text = index_.doc_text(doc_id);
        std::string_view title = index_.doc_title(doc_id);

        auto matches_title = reg.find_all(title);
        auto matches_text = reg.find_all(text);
        const size_t total_matches = matches_title.size() + matches_text.size();

        if (total_matches > 0) {
            hits.push_back(SearchHit{.doc_id = doc_id, .score = static_cast<float>(total_matches)});
        }
    }

    std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.doc_id < b.doc_id;
    });

    if (hits.size() > top_k) {
        hits.resize(top_k);
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    const size_t took =
        static_cast<size_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    return HybridSearchResult{.hits = std::move(hits),
                              .took_us = took,
                              .query_type = HybridQueryType::Regex,
                              .correction_suggestion = ""};
}

}  // namespace needlefish
