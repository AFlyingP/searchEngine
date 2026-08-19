#pragma once

/**
 * @file needlefish.hpp
 * @brief Needlefish: High-Performance C++20 Search & Succinct Indexing Engine.
 * Single umbrella header for embedding Needlefish into C++ applications.
 */

// Bitvectors & Wavelet Trees
#include "bitvector/bitvector.hpp"
#include "wavelet/wavelet_tree.hpp"

// Suffix Array & FM-Index
#include "sa/sais.hpp"
#include "fm/fm_index.hpp"

// Compression & Lexicon
#include "invidx/compression.hpp"
#include "invidx/postings.hpp"
#include "invidx/radix_trie.hpp"
#include "invidx/index_builder.hpp"

// Query Evaluation & Ranking
#include "rank/bm25.hpp"
#include "rank/wand.hpp"
#include "rank/query_eval.hpp"
#include "rank/snippet.hpp"
#include "rank/hybrid_search.hpp"

// Automata & Typo Tolerance
#include "automata/levenshtein.hpp"
#include "automata/autocomplete.hpp"
#include "automata/regex.hpp"

// Store & Server
#include "store/index_file.hpp"
#include "store/mmap.hpp"
#include "server/http_server.hpp"
#include "util/analyzer.hpp"
#include "util/utf8.hpp"
#include "util/porter_stemmer.hpp"
