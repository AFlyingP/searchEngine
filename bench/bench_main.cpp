#include <benchmark/benchmark.h>
#include <random>
#include <string>
#include <vector>

#include "bitvector/bitvector.hpp"
#include "fm/fm_index.hpp"
#include "invidx/compression.hpp"
#include "sa/sais.hpp"
#include "wavelet/wavelet_tree.hpp"

using namespace needlefish;

// 1. BitVector Rank1 Benchmark
static void BM_BitVector_Rank1(benchmark::State& state) {
    const size_t num_bits = 1'000'000;
    BitVector bv(num_bits);
    std::mt19937_64 rng(42);
    for (size_t i = 0; i < num_bits; i += 2) {
        bv.set(i, true);
    }
    RankSelectBitVector rsbv(std::move(bv));

    std::vector<size_t> query_indices(10000);
    std::uniform_int_distribution<size_t> dist(0, num_bits);
    for (auto& idx : query_indices) {
        idx = dist(rng);
    }

    size_t i = 0;
    for (auto _ : state) {
        size_t idx = query_indices[i++ % query_indices.size()];
        benchmark::DoNotOptimize(rsbv.rank1(idx));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BitVector_Rank1);

// 2. BitVector Select1 Benchmark
static void BM_BitVector_Select1(benchmark::State& state) {
    const size_t num_bits = 1'000'000;
    BitVector bv(num_bits);
    std::mt19937_64 rng(42);
    for (size_t i = 0; i < num_bits; i += 2) {
        bv.set(i, true);
    }
    RankSelectBitVector rsbv(std::move(bv));

    std::vector<size_t> query_ks(10000);
    std::uniform_int_distribution<size_t> dist(1, rsbv.total_ones());
    for (auto& k : query_ks) {
        k = dist(rng);
    }

    size_t i = 0;
    for (auto _ : state) {
        size_t k = query_ks[i++ % query_ks.size()];
        benchmark::DoNotOptimize(rsbv.select1(k));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BitVector_Select1);

// 3. SA-IS Construction Benchmark
static void BM_SAIS_Construction_10MB(benchmark::State& state) {
    const size_t size = 10 * 1024 * 1024;  // 10 MB
    std::string text(size, '\0');
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<int> dist('a', 'z');
    for (size_t i = 0; i < size; ++i) {
        text[i] = static_cast<char>(dist(rng));
    }

    for (auto _ : state) {
        auto sa = build_suffix_array(text);
        benchmark::DoNotOptimize(sa);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(size));
}
BENCHMARK(BM_SAIS_Construction_10MB)->Unit(benchmark::kMillisecond);

// 4. FM-Index Backward Search (10-char pattern)
static void BM_FMIndex_BackwardSearch_10Char(benchmark::State& state) {
    const size_t size = 1024 * 1024;  // 1 MB
    std::string text(size, '\0');
    std::mt19937_64 rng(54321);
    std::uniform_int_distribution<int> dist('a', 'h');
    for (size_t i = 0; i < size; ++i) {
        text[i] = static_cast<char>(dist(rng));
    }

    FMIndex fmi(text, 32);

    std::vector<std::string> patterns;
    for (int i = 0; i < 1000; ++i) {
        std::string p;
        for (int j = 0; j < 10; ++j)
            p += static_cast<char>(dist(rng));
        patterns.push_back(p);
    }

    size_t i = 0;
    for (auto _ : state) {
        const auto& p = patterns[i++ % patterns.size()];
        benchmark::DoNotOptimize(fmi.count(p));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FMIndex_BackwardSearch_10Char);

// 5. FM-Index Locate Matches
static void BM_FMIndex_Locate(benchmark::State& state) {
    const size_t size = 1024 * 1024;  // 1 MB
    std::string text(size, '\0');
    std::mt19937_64 rng(999);
    std::uniform_int_distribution<int> dist('a', 'd');
    for (size_t i = 0; i < size; ++i) {
        text[i] = static_cast<char>(dist(rng));
    }

    FMIndex fmi(text, 32);

    for (auto _ : state) {
        auto matches = fmi.locate("abcd");
        benchmark::DoNotOptimize(matches);
    }
    state.SetItemsProcessed(state.iterations());
}
// 6. Posting Decode Benchmark (Scalar vs SIMD, 1M postings)
static void BM_Posting_Decode_Scalar(benchmark::State& state) {
    const size_t num_blocks = 8000;  // ~1,024,000 postings
    std::vector<uint32_t> test_data(num_blocks * 128);
    std::mt19937 rng(42);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = rng() % 255;  // 8-bit width
    }

    std::vector<uint8_t> packed_blocks(num_blocks * 16 * 8);
    for (size_t b = 0; b < num_blocks; ++b) {
        BitPacking::pack128(&test_data[b * 128], &packed_blocks[b * 16 * 8], 8);
    }

    uint32_t out[128];
    for (auto _ : state) {
        for (size_t b = 0; b < num_blocks; ++b) {
            BitPacking::unpack128_scalar(&packed_blocks[b * 16 * 8], out, 8);
            benchmark::DoNotOptimize(out);
        }
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(num_blocks * 128));
}
BENCHMARK(BM_Posting_Decode_Scalar);

static void BM_Posting_Decode_SIMD(benchmark::State& state) {
    const size_t num_blocks = 8000;  // ~1,024,000 postings
    std::vector<uint32_t> test_data(num_blocks * 128);
    std::mt19937 rng(42);
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = rng() % 255;  // 8-bit width
    }

    std::vector<uint8_t> packed_blocks(num_blocks * 16 * 8);
    for (size_t b = 0; b < num_blocks; ++b) {
        BitPacking::pack128(&test_data[b * 128], &packed_blocks[b * 16 * 8], 8);
    }

    uint32_t out[128];
    for (auto _ : state) {
        for (size_t b = 0; b < num_blocks; ++b) {
            BitPacking::unpack128(&packed_blocks[b * 16 * 8], out, 8);
            benchmark::DoNotOptimize(out);
        }
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(num_blocks * 128));
}
#include "automata/levenshtein.hpp"
#include "automata/regex.hpp"
#include "invidx/radix_trie.hpp"

// 7. Regex Linear-Time Matching Benchmark
static void BM_Regex_Match(benchmark::State& state) {
    const size_t text_len = 100000;
    std::string text(text_len, 'a');
    for (size_t i = 1000; i < text_len; i += 2000) {
        text[i] = 'b';
        text[i + 1] = 'c';
    }

    Regex r("a*b+c");

    for (auto _ : state) {
        auto matches = r.find_all(text);
        benchmark::DoNotOptimize(matches);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(text_len));
}
BENCHMARK(BM_Regex_Match);

// 8. Levenshtein Trie Lockstep Intersection Benchmark (50,000 terms)
static void BM_Levenshtein_Trie_Intersect(benchmark::State& state) {
    RadixTrie trie;
    std::mt19937 rng(42);
    const std::string alphabet = "abcdefghijklmnopqrstuvwxyz";

    for (uint32_t i = 0; i < 50000; ++i) {
        const size_t len = 5 + (rng() % 10);
        std::string word;
        for (size_t j = 0; j < len; ++j) {
            word += alphabet[rng() % alphabet.size()];
        }
        trie.insert(word, TermPayload{.term_id = i, .doc_freq = 1 + (rng() % 100)});
    }

    LevenshteinAutomaton dfa("algorithm", 2);

    for (auto _ : state) {
        auto matches = dfa.match_trie(trie, 20);
        benchmark::DoNotOptimize(matches);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Levenshtein_Trie_Intersect);

BENCHMARK_MAIN();
