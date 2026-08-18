#include <benchmark/benchmark.h>
#include "bitvector/bitvector.hpp"
#include "fm/fm_index.hpp"
#include "sa/sais.hpp"
#include "wavelet/wavelet_tree.hpp"

#include <random>
#include <string>
#include <vector>

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
        for (int j = 0; j < 10; ++j) p += static_cast<char>(dist(rng));
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
BENCHMARK(BM_FMIndex_Locate);

BENCHMARK_MAIN();
