# Needlefish Phase 1 Microbenchmarks

Measured on: AMD Ryzen 5 / 12 vCPU @ 3.7 GHz (GCC 16.2.0, `-O3 -march=native`)

## Succinct Operations & Index Construction Benchmarks

| Benchmark Component | Metric / Operation | Result | Target / Standard | Status |
| :--- | :--- | :--- | :--- | :--- |
| **RankSelectBitVector** | `rank1(i)` latency | **1.99 ns / op** | < 5 ns hot | **Exceeded** (2.5x faster) |
| **RankSelectBitVector** | `rank1` throughput | **508.37 M ops / s** | > 200 M ops / s | **Exceeded** |
| **RankSelectBitVector** | `select1(k)` latency | **48.8 ns / op** | < 100 ns | **Passed** |
| **Byte Wavelet Tree** | `access(i)` latency | **16.2 ns / op** | < 50 ns | **Passed** |
| **Byte Wavelet Tree** | `rank(c, i)` latency | **15.8 ns / op** | < 50 ns | **Passed** |
| **SA-IS Suffix Array** | Construction throughput | **10.16 MB / s** | O(n) linear-time | **Passed** |
| **FM-Index** | Backward Search (10-char) | **0.988 µs / query** | < 5 µs | **Exceeded** (5x faster) |
| **FM-Index** | Backward Search throughput | **995.56 k queries / s** | > 200 k queries / s | **Exceeded** |

---

## Verification & Oracle Correctness Matrix

| Component | Test Suite | Oracle / Method | Scale / Iterations | Status |
| :--- | :--- | :--- | :--- | :--- |
| **BitVector** | `test_bitvector.cpp` | Exact boundary fixtures | 64, 512, 1024-bit boundaries | **Passed** |
| **BitVector** | `prop_bitvector.cpp` | Prefix-sum dynamic array | $1,000,000$ random ops (1%, 50%, 99% densities) | **Passed** |
| **Wavelet Tree** | `test_wavelet.cpp` | Character access, rank, select roundtrip | Boundary ASCII & binary alphabets | **Passed** |
| **Wavelet Tree** | `prop_wavelet.cpp` | Linear scan brute-force oracle | $200,000$ operations on random byte buffers | **Passed** |
| **SA-IS** | `test_sais.cpp` | Known answer (`"mmiissiissiippii"`), Repetitive, De Bruijn | Classic & adversarial inputs | **Passed** |
| **SA-IS** | `prop_sais.cpp` | Naive suffix sort $O(n^2 \log n)$ | 100 randomized text trials | **Passed** |
| **SA-IS** | `prop_sais.cpp` | Larsson-Sadakane prefix doubling $O(n \log n)$ | $100,000$-character differential test | **Passed** |
| **FM-Index** | `test_fm_index.cpp` | Locate, Count, Snippet extract | Exact text reconstructions | **Passed** |
| **FM-Index** | `prop_fm_index.cpp` | `std::string::find` brute-force scans | $10,000$ randomized multi-pattern queries | **Passed** |
