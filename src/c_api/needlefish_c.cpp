#include "needlefish_c.h"
#include "store/index_file.hpp"
#include "rank/query_eval.hpp"
#include "rank/snippet.hpp"
#include "automata/autocomplete.hpp"
#include "util/analyzer.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct needlefish_index_t {
    needlefish::IndexView index;
    std::unique_ptr<needlefish::QueryEvaluator> evaluator;
    std::unique_ptr<needlefish::AutocompleteEngine> autocompleter;
};

struct InternalSearchResult : public needlefish_search_result_t {
    std::vector<std::string> titles;
    std::vector<std::string> snippets;
    std::vector<needlefish_hit_t> hit_array;
};

struct InternalSuggestResult : public needlefish_suggest_result_t {
    std::vector<std::string> string_storage;
    std::vector<char*> ptr_array;
};

extern "C" {

needlefish_index_t* needlefish_open(const char* index_path) {
    if (!index_path) return nullptr;
    try {
        auto handle = std::make_unique<needlefish_index_t>();
        handle->index.open(index_path);
        handle->evaluator = std::make_unique<needlefish::QueryEvaluator>(handle->index);
        handle->autocompleter = std::make_unique<needlefish::AutocompleteEngine>(handle->index);
        return handle.release();
    } catch (...) {
        return nullptr;
    }
}

void needlefish_close(needlefish_index_t* handle) {
    delete handle;
}

uint32_t needlefish_total_docs(const needlefish_index_t* handle) {
    if (!handle) return 0;
    return handle->index.stats().total_docs;
}

uint64_t needlefish_total_tokens(const needlefish_index_t* handle) {
    if (!handle) return 0;
    return handle->index.stats().total_tokens;
}

needlefish_search_result_t* needlefish_search(needlefish_index_t* handle,
                                             const char* query,
                                             size_t top_k) {
    if (!handle || !query || !handle->evaluator) return nullptr;

    try {
        auto q_res = handle->evaluator->search(query, top_k);
        auto res = std::make_unique<InternalSearchResult>();
        res->num_hits = q_res.hits.size();
        res->took_us = q_res.took_us;

        res->titles.reserve(res->num_hits);
        res->snippets.reserve(res->num_hits);
        res->hit_array.reserve(res->num_hits);

        needlefish::SnippetGenerator snippet_gen(140);
        needlefish::Analyzer analyzer(true, true);
        auto tokens = analyzer.analyze(query);
        std::vector<std::string> query_terms;
        query_terms.reserve(tokens.size());
        for (const auto& t : tokens) {
            query_terms.push_back(t.term);
        }

        for (const auto& hit : q_res.hits) {
            std::string title = std::string(handle->index.doc_title(hit.doc_id));
            std::string snippet = snippet_gen.highlight(handle->index.doc_text(hit.doc_id), query_terms);
            
            res->titles.push_back(title);
            res->snippets.push_back(snippet);

            needlefish_hit_t c_hit;
            c_hit.doc_id = hit.doc_id;
            c_hit.score = hit.score;
            c_hit.title = res->titles.back().c_str();
            c_hit.snippet = res->snippets.back().c_str();

            res->hit_array.push_back(c_hit);
        }

        res->hits = res->hit_array.data();
        return res.release();
    } catch (...) {
        return nullptr;
    }
}

void needlefish_free_search_result(needlefish_search_result_t* result) {
    delete static_cast<InternalSearchResult*>(result);
}

needlefish_suggest_result_t* needlefish_suggest(needlefish_index_t* handle,
                                               const char* prefix,
                                               size_t max_results) {
    if (!handle || !prefix || !handle->autocompleter) return nullptr;

    try {
        auto suggestions = handle->autocompleter->prefix_suggest(prefix, max_results);
        auto res = std::make_unique<InternalSuggestResult>();
        res->num_suggestions = suggestions.size();
        res->string_storage.reserve(suggestions.size());
        res->ptr_array.reserve(suggestions.size());

        for (const auto& s : suggestions) {
            res->string_storage.push_back(s.text);
        }
        for (auto& s : res->string_storage) {
            res->ptr_array.push_back(s.data());
        }

        res->suggestions = res->ptr_array.data();
        return res.release();
    } catch (...) {
        return nullptr;
    }
}

void needlefish_free_suggest_result(needlefish_suggest_result_t* result) {
    delete static_cast<InternalSuggestResult*>(result);
}

}
