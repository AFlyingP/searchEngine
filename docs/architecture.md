# Needlefish Architecture RFC & Design Specification

## Abstract
Needlefish is an embedded, zero-external-dependency, succinct search and substring indexing engine implemented in modern ISO C++20. It unifies classical inverted indexes (Okapi BM25 with Block-Max WAND dynamic pruning) and succinct full-text indexing structures (linear-time SA-IS suffix array construction, Burrows-Wheeler Transform, and wavelet tree-backed FM-Index) within a single zero-copy memory-mapped container format (`.idx`).

---

## 1. System Architecture Diagram

```text
                                  +---------------------------------------+
                                  |  HTTP REST Server / C & Python APIs   |
                                  |   GET /api/search, /api/suggest, etc. |
                                  +---------------------------------------+
                                                     |
                         +---------------------------+---------------------------+
                         |                                                       |
                         v                                                       v
         +-------------------------------+                       +-------------------------------+
         |    Ranked Retrieval Engine    |                       |   Fuzzy & Automata Engine     |
         |    - Okapi BM25 Scorer        |                       |   - Levenshtein DP-Row (k=1,2)|
         |    - Block-Max WAND Pruner    |                       |   - Flattened Radix Trie      |
         |    - SIMD-BP128 Postings      |                       |   - Regex NFA/DFA Search      |
         +-------------------------------+                       +-------------------------------+
                         |                                                       |
                         +---------------------------+---------------------------+
                                                     |
                                                     v
                                  +---------------------------------------+
                                  |      Succinct Substring Engine        |
                                  |   - Linear-time SA-IS Suffix Array    |
                                  |   - Burrows-Wheeler Transform (BWT)   |
                                  |   - Bit-Sliced Wavelet Tree (Occ)     |
                                  |   - Rank9 / Select O(1) Bitvectors    |
                                  +---------------------------------------+
                                                     |
                                                     v
                                  +---------------------------------------+
                                  |   Single-File Zero-Copy Storage       |
                                  |   - Fixed Header (NFLSHIDX, v2)       |
                                  |   - 64-byte Cache-Aligned Sections    |
                                  |   - Zero Heap Allocation on Startup   |
                                  +---------------------------------------+
```

---

## 2. Core Subsystems & Theoretical Foundations

### 2.1 Succinct Structures & Substring Search
- **Rank9 Bitvector**: 64-bit word storage paired with two-level interleaved superblocks (512-bit blocks with 8x64-bit relative counts). Delivers $O(1)$ rank evaluation with $< 25\%$ space overhead.
- **Wavelet Tree**: Bit-sliced binary tree over the byte alphabet $\Sigma = [0, 255]$. Computes $\text{rank}_c(S, i)$ and $\text{access}(S, i)$ in $O(\log |\Sigma|) = 8$ bitvector operations.
- **SA-IS Suffix Array Construction**: Linear-time $O(n)$ suffix array generation using induced sorting over LMS (leftmost S-type) characters with $\sim 8n\text{--}9n$ bytes working memory in 32-bit mode.
- **FM-Index & BWT**: Combines BWT with Wavelet Tree Occ-tables to support exact arbitrary substring search in $O(m)$ time where $m$ is the query pattern length, completely independent of total corpus size.

### 2.2 Inverted Index & Ranking Pipeline
- **SIMD Bit-Packing**: 128-docID posting blocks compressed with bit-width delta encoding, decoding at over 413 million postings per second using SSE2 vector instructions with scalar fallback.
- **Block-Max WAND Pruning**: Computes upper-bound scores $\max(U_b)$ per posting block, skipping non-competitive blocks to achieve a 7.68x speedup over naive disjunctions.
- **Flattened Radix Trie**: Cache-aligned node structures laid out contiguously in memory for zero-pointer traversal and lockstep intersection.

### 2.3 Parametric Levenshtein Automata
- **Levenshtein DP-Row Automaton**: Evaluates dynamic programming row transitions directly across trie edges, bounding edit distance $k \in \{1, 2\}$.
- **Lockstep Trie Intersection**: Intersects automaton states directly against trie transitions, pruning invalid candidate subtrees in $O(|q|)$ time without scanning the full lexicon.

### 2.4 Server Concurrency & Memory Safety
- **Worker Thread Pool**: Multi-threaded socket handling with an 8-worker thread pool and bounded task queue (128 connections max, responding 503 on saturation).
- **Socket Ownership & Framing**: Workers own client sockets end-to-end and enforce a 15-second wall-clock framing deadline (408 on timeout).
- **Thread Safety**: Workers safely share a single `const IndexView` with `Regex` DFA state caches protected by a mutex.

---

## 3. Storage Format Layout (`.idx`)

```text
+------------------------------------------------------------------------+
| Fixed Magic Header (Magic: 'N','F','L','S','H','I','D','X', Version 2) |
+------------------------------------------------------------------------+
| Section Header Table: Section IDs, Offsets, Lengths, CRC32 Checksums   |
+------------------------------------------------------------------------+
| Section 1: Stats (Total docs, tokens, avg_doc_len, k1, b)              |
+------------------------------------------------------------------------+
| Section 2: DocMetadata (External doc IDs, token counts, title spans)   |
+------------------------------------------------------------------------+
| Section 3: StoredFields (Raw UTF-8 document titles and text)           |
+------------------------------------------------------------------------+
| Section 4: TermDict (Contiguous Radix Trie Nodes & String Pool)        |
+------------------------------------------------------------------------+
| Section 5: Postings (Bit-packed 128-docID blocks with Block-Max scores)|
+------------------------------------------------------------------------+
| Section 6: Positions (Varint delta-encoded term positions)             |
+------------------------------------------------------------------------+
| Section 7: FMIndex (BWT Wavelet Tree & Sampled Suffix Array)           |
+------------------------------------------------------------------------+
```

---

## 4. Failure Modes & Graceful Degradation
1. **Truncated/Corrupted Files**: Validates magic signature (`NFLSHIDX`), format version (2), and CRC32 checksums during `IndexView::open`, throwing descriptive C++ exceptions without segfaults or undefined behavior.
2. **Exhausted Conjunction Readers**: Galloping posting readers evaluate terminal conditions on every iteration, breaking immediately when any reader is exhausted.
3. **Extreme Queries**: Gracefully handles zero-length inputs, single-character fuzzy inputs, and 500+ token expressions with bounded heap usage.
