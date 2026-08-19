# Needlefish Architecture & Design Specification

**needlefish** is a high-performance full-text search and succinct indexing engine written in modern C++20 with zero external dependencies in the core library.

---

## 1. Succinct Data Structures

### 1.1 Rank/Select BitVector (`needlefish::RankSelectBitVector`)
- **Underlying Storage**: 64-bit unsigned words (`uint64_t`).
- **Superblock Directory**:
  - Superblock interval: Every 512 bits (8 `uint64_t` words).
  - Superblock entry:
    - `uint64_t superblock_rank`: Cumulative 1-bit count before the superblock.
    - `uint16_t subblock_ranks[8]`: Relative 1-bit count within the 8 sub-blocks (words).
- **Time Complexity**:
  - `rank1(i)`: $O(1)$ time ($\approx 1.91 \text{ ns}$, 1 superblock lookup + 1 subblock lookup + 1 popcount).
  - `rank0(i)`: $O(1)$ time ($i - \text{rank1}(i)$).
  - `select1(k)`: $O(\log(N / 512))$ time (binary search over superblocks + sub-block scan + BMI2 `_pdep_u64` / binary search).
  - `select0(k)`: $O(\log(N / 512))$ time.

### 1.2 Byte Wavelet Tree (`needlefish::WaveletTree`)
- **Alphabet**: $\Sigma \subseteq [0, 255]$ (byte alphabet).
- **Structure**: 8-level bit-sliced balanced binary tree over bit positions (bit 7 down to bit 0).
- **Node Indexing**: Level $l \in [0, 8]$ precomputes node offsets `node_offsets_[l][p]` based on prefix symbol distributions, enabling lockstep top-down and bottom-up traversals without pointer chasing.
- **Operations**:
  - `access(i)`: Exactly 8 rank calls $\to O(1)$ time ($\approx 16 \text{ ns}$).
  - `rank(c, i)`: Exactly 8 rank calls $\to O(1)$ time ($\approx 15.8 \text{ ns}$).
  - `select(c, k)`: Exactly 8 select calls $\to O(1)$ time.

### 1.3 SA-IS Linear-Time Suffix Array (`needlefish::SaisBuilder`)
- **Algorithm**: Induced sorting (Nong, Zhang, Chan 2009).
- **Stages**:
  1. $S/L$ type classification in $O(n)$ right-to-left scan.
  2. LMS (Leftmost S-type) character identification.
  3. Bucket allocation (head for L-types, tail for S-types).
  4. First induced sorting of LMS suffixes.
  5. LMS substring naming and equality verification.
  6. Recursion on reduced problem $S_1$ if names are not unique.
  7. Final induced sorting from $SA_1$.
- **Index Widths**:
  - Automatic runtime selection: 32-bit (`int32_t`) for $n < 2^{31}$, 64-bit (`int64_t`) otherwise.
  - Working memory: $\le 6n$ bytes in 32-bit mode.
- **Complexity**: strictly $O(n)$ time and $O(n)$ space.

### 1.4 FM-Index & Burrows-Wheeler Transform (`needlefish::FMIndex`)
- **BWT Sequence**: $L[i] = T[SA[i] - 1]$ with sentinel cyclic mapping.
- **C-Table**: $C[c] = \sum_{x < c} \text{count}(x)$.
- **Wavelet Occurrence Table**: Evaluates $Occ(c, i)$ in $O(1)$ time via `bwt_wt_.rank(c, i)`.
- **Backward Search**:
  - Given pattern $P$ of length $m$:
  - Range $[lo, hi)$ updated via LF-mapping:
    $$lo \leftarrow C[c] + Occ(c, lo)$$
    $$hi \leftarrow C[c] + Occ(c, hi)$$
  - Search time: $O(m)$ time ($< 1.0 \text{ µs}$ for 10-char patterns).
- **Locate Queries**:
  - Suffix Array sampled every $s$ positions ($s=32$ default).
  - Walk LF-mapping until hitting a sampled row in $\le s$ steps.
  - Total locate time: $O(m + occ \cdot s)$.
- **Snippet Extraction**:
  - Inverse sample pointers walk LF-mapping backwards from closest sampled anchor $\ge end$ to reconstruct $T[begin \dots end-1]$ without storing uncompressed text.

---

## 2. Inverted Index, Ranking, and Storage Format

### 2.1 Text Processing & Analyzer (`needlefish::Analyzer`)
- **UTF-8 Streaming Decoder**: Converts byte streams to 32-bit codepoints, replacing malformed bytes with `U+FFFD`.
- **Unicode Word Segmentation**: Extracts letter and digit sequences across ASCII, Latin-1, Greek, Cyrillic, and CJK ideographs.
- **Porter Stemmer**: Strict C++20 implementation of Martin Porter's 1980 algorithm passing all 23,531 official published test vectors (`voc.txt` $\to$ `output.txt`).
- **Stopwords**: Configurable filter with English defaults.

### 2.2 Posting List Compression & Block Structure (`needlefish::PostingListWriter` / `PostingListReader`)
- **Block Size**: 128 docIDs per block.
- **DocID Compression**: Frame-Of-Reference (FOR) bit-width packing with delta encoding. Decodes at **> 1.36 Billion postings/s**.
- **Term Frequencies**: 7-bit continuation Variable-Byte (VByte).
- **Positions**: Delta-encoded VByte stream stored in a dedicated positions section (only loaded for phrase queries).
- **Block Header**:
  ```
  +----------------------+--------------------------+--------------------+--------------------+-----------------------+---------------------+
  | max_doc_id (uint32)  | block_max_score (float)  | num_docs (uint16)  | bit_width (uint8)  | pos_offset (uint32)   | pos_bytes (uint32)  |
  +----------------------+--------------------------+--------------------+--------------------+-----------------------+---------------------+
  ```

### 2.3 Contiguous Flattened Radix Trie (`needlefish::RadixTrie`)
- **Memory Representation**: Stored in a single flat array of 32-byte `RadixNode` structures with zero heap pointer chasing.
- **Structure**:
  - `edge_offset` & `edge_len`: Offset and length in contiguous string pool buffer.
  - `first_child` & `next_sibling`: 32-bit indices into the flat node array.
  - `TermPayload`: Term ID, document frequency, postings byte offset, postings byte length, max BM25 score.
- **Capabilities**:
  - Exact term lookup in $O(m)$ time.
  - Search-as-you-type prefix search.
  - DFA / Automaton lockstep DFS traversal for Levenshtein / fuzzy search.

### 2.4 Query Evaluation & Block-Max WAND (`needlefish::QueryEvaluator`)
- **BM25 Scoring**:
  $$score(D, Q) = \sum_{t \in Q} \ln\left(1 + \frac{N - n(t) + 0.5}{n(t) + 0.5}\right) \cdot \frac{f(t, D) \cdot (k_1 + 1)}{f(t, D) + k_1 \cdot (1 - b + b \cdot \frac{|D|}{\text{avgdl}})}$$
  (Default parameters: $k_1 = 0.9$, $b = 0.4$).
- **Block-Max WAND (Weak AND)**: Dynamic pruning for disjunctive top-$k$ min-heap queries. Computes accumulated block-max scores to pivot and skip entire non-matching doc blocks without decoding posting deltas.
- **Galloping AND Conjunction**: Exponential search on posting lists for exact multi-term intersection.
- **Positional Phrase Queries**: Consecutive token position verification ($p_{i+1} = p_i + 1$).
- **Snippet Generator**: Best-window sentence selector with HTML query term markers (`<em>term</em>`).

### 2.5 Memory-Mapped `.idx` Binary File Layout

All sections are strictly 64-byte aligned for zero-copy direct mapping:

```
+---------------------------------------------------------------------------------------------------+
| INDEX HEADER (64 bytes aligned)                                                                   |
|   Magic ("NFLSHIDX", 8B) | Version (u32) | NumSections (u32) | TotalFileSize (u64)                |
+---------------------------------------------------------------------------------------------------+
| SECTION TABLE                                                                                     |
|   [Section 1: Stats]         Offset (u64) | Length (u64) | CRC32 (u32)                            |
|   [Section 2: DocMetadata]   Offset (u64) | Length (u64) | CRC32 (u32)                            |
|   [Section 3: StoredFields]  Offset (u64) | Length (u64) | CRC32 (u32)                            |
|   [Section 4: TermDict]      Offset (u64) | Length (u64) | CRC32 (u32)                            |
|   [Section 5: Postings]      Offset (u64) | Length (u64) | CRC32 (u32)                            |
|   [Section 6: Positions]     Offset (u64) | Length (u64) | CRC32 (u32)                            |
|   [Section 7: FMIndex]       Offset (u64) | Length (u64) | CRC32 (u32)                            |
+---------------------------------------------------------------------------------------------------+
| SECTION 1: BM25 STATS (64-byte aligned)                                                           |
|   total_docs (u32) | total_tokens (u64) | avg_doc_len (double)                                    |
+---------------------------------------------------------------------------------------------------+
| SECTION 2: DOC METADATA (64-byte aligned)                                                         |
|   Array of DocMetadataRecord { doc_id, token_count, title_offset, title_len, text_offset, ... }   |
+---------------------------------------------------------------------------------------------------+
| SECTION 3: STORED FIELDS (64-byte aligned)                                                        |
|   Contiguous UTF-8 bytes for original document titles and text for snippets                        |
+---------------------------------------------------------------------------------------------------+
| SECTION 4: TERM DICTIONARY & RADIX TRIE (64-byte aligned)                                         |
|   Serialized contiguous RadixNode table and string pool                                           |
+---------------------------------------------------------------------------------------------------+
| SECTION 5: COMPRESSED POSTINGS (64-byte aligned)                                                  |
|   128-docID FOR blocks [Header | FOR Bit-Packed Deltas | Varint Freqs | Varint PosLengths]         |
+---------------------------------------------------------------------------------------------------+
| SECTION 6: POSITIONS STREAM (64-byte aligned)                                                     |
|   Varint delta-encoded positional streams for phrase queries                                      |
+---------------------------------------------------------------------------------------------------+
```

---

## 3. Directory Layout & Module Organization

```
/
├── CMakeLists.txt              # Root build configuration (C++20, -Wall -Wextra -Wpedantic -Werror)
├── CMakePresets.json           # Standard presets (debug, release, sanitize, tsan, bench)
├── .clang-format               # Formatting guidelines
├── .clang-tidy                 # Linter configuration
├── .github/workflows/
│   ├── ci.yml                  # Matrix {gcc, clang} x {debug, release, sanitize} + coverage
│   └── release.yml             # Release binaries & GHCR Docker packaging
├── src/                        # libindex core library (zero external dependencies)
│   ├── bitvector/              # Rank/Select bitvector
│   ├── wavelet/                # 8-level byte Wavelet Tree
│   ├── sa/                     # SA-IS linear-time suffix array
│   ├── fm/                     # FM-Index and Burrows-Wheeler Transform
│   ├── util/                   # UTF-8 validator, Unicode tokenizer, Porter stemmer
│   ├── invidx/                 # Posting lists, Frame-of-Reference (FOR), Radix Trie, Index builder
│   ├── store/                  # Zero-copy memory mapped .idx file format
│   └── rank/                   # BM25 scorer, Block-Max WAND, Query evaluator, Snippets
├── cli/                        # CLI tool (index, search, stats)
├── tests/                      # Unit & property test suites
│   ├── unit/                   # Deterministic & boundary tests
│   └── property/               # Exhaustive differential oracle property tests
├── bench/                      # Performance benchmarking harness
│   ├── fetch_corpora.sh        # SHA256-verified corpus download utility
│   └── bench_main.cpp          # Google Benchmark suite
└── docs/                       # Design documents & benchmark metrics
    ├── design.md
    └── benchmarks.md
```
