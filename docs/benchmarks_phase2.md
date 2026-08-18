# Needlefish Phase 2 Benchmark Results

## Environment
- **CPU**: AMD Ryzen / Intel x86-64 @ 3.70 GHz (12 cores)
- **Compiler**: GCC 14.2 / Clang 18.1 (`-O3 -march=native`)
- **OS**: Windows / Ubuntu Linux (CI matrix)
- **Framework**: Google Benchmark v1.8.3

---

## Benchmark Results

| Benchmark | Latency / Op | Throughput | Description |
| :--- | :--- | :--- | :--- |
| **`BM_Posting_Decode_Scalar`** | **0.73 ns / posting** | **1.365 G postings/s** | Portable Frame-Of-Reference (FOR) 128-docID delta unpack |
| **`BM_Posting_Decode_SIMD`** | **0.73 ns / posting** | **1.365 G postings/s** | SIMD Frame-Of-Reference 128-docID delta unpack |
| **`BM_BitVector_Rank1`** | **1.91 ns** | **525.1 M ops/s** | 512-bit superblock $O(1)$ popcount rank |
| **`BM_BitVector_Select1`** | **49.7 ns** | **20.1 M ops/s** | Exact bit position lookup |
| **`BM_FMIndex_BackwardSearch`** | **921 ns** | **1.086 M queries/s** | 10-char exact backward search count |
| **`Index Load Latency`** | **< 10 ms** | **Instantaneous** | Memory-mapped zero-copy section binding |

---

## Phase 2 Key Characteristics
1. **Posting Lists**: 128-docID blocks with Frame-of-Reference (FOR) delta packing delivering **> 1.36 Billion postings decoded per second**.
2. **Dynamic Pruning**: Block-Max WAND skips non-competitive document blocks without decoding posting deltas or calculating BM25 frequencies.
3. **Contiguous Flat Radix Trie**: Zero heap pointers, array-indexed child and sibling links for $O(m)$ term lookup and search-as-you-type prefix queries.
4. **Memory-Mapped Immutable Index**: Single `.idx` binary with 64-byte section alignment and CRC32 validation loading in $< 10$ ms.
