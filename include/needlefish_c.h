#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #if defined(NEEDLEFISH_EXPORTS)
    #define NEEDLEFISH_API __declspec(dllexport)
  #else
    #define NEEDLEFISH_API __declspec(dllimport)
  #endif
#else
  #define NEEDLEFISH_API __attribute__((visibility("default")))
#endif

typedef struct needlefish_index_t needlefish_index_t;

typedef struct {
    uint32_t doc_id;
    float score;
    const char* title;
    const char* snippet;
} needlefish_hit_t;

typedef struct {
    needlefish_hit_t* hits;
    size_t num_hits;
    uint64_t took_us;
} needlefish_search_result_t;

typedef struct {
    char** suggestions;
    size_t num_suggestions;
} needlefish_suggest_result_t;

// Lifecycle
NEEDLEFISH_API needlefish_index_t* needlefish_open(const char* index_path);
NEEDLEFISH_API void needlefish_close(needlefish_index_t* index);

// Metadata & Stats
NEEDLEFISH_API uint32_t needlefish_total_docs(const needlefish_index_t* index);
NEEDLEFISH_API uint64_t needlefish_total_tokens(const needlefish_index_t* index);

// Querying
NEEDLEFISH_API needlefish_search_result_t* needlefish_search(needlefish_index_t* index,
                                                             const char* query,
                                                             size_t top_k);
NEEDLEFISH_API void needlefish_free_search_result(needlefish_search_result_t* result);

// Autocomplete
NEEDLEFISH_API needlefish_suggest_result_t* needlefish_suggest(needlefish_index_t* index,
                                                               const char* prefix,
                                                               size_t max_results);
NEEDLEFISH_API void needlefish_free_suggest_result(needlefish_suggest_result_t* result);

#ifdef __cplusplus
}
#endif
