# How Needlefish Works: Architectural Overview

Needlefish is a zero-dependency, memory-mapped full-text and pattern search engine written in C++20. This document provides a high-level explanation of its indexing pipeline, query execution engine, and on-disk representation.

---

## 1. Unified On-Disk Format (`.idx`)

Needlefish indexes are serialized into a single immutable binary file mapped directly into virtual memory (`mmap`).

```
+-------------------------------------------------------------+
| Header (NFLSHIDX magic, version 2, num_sections, file_size)  |
+-------------------------------------------------------------+
| Section Table (Stats, DocMetadata, StoredFields, etc.)      |
+-------------------------------------------------------------+
| [Section 1: Stats]         - Total docs, tokens, avg doc len|
+-------------------------------------------------------------+
| [Section 2: DocMetadata]   - DocMetadataRecord array        |
+-------------------------------------------------------------+
| [Section 3: StoredFields]  - Raw document titles & text     |
+-------------------------------------------------------------+
| [Section 4: TermDict]      - Flat memory-mapped RadixTrie   |
+-------------------------------------------------------------+
| [Section 5: Postings]      - Block-packed postings (128)    |
+-------------------------------------------------------------+
| [Section 6: Positions]     - Varint delta-encoded positions |
+-------------------------------------------------------------+
| [Section 7: FMIndex]       - (Optional) BWT & Wavelet Tree  |
+-------------------------------------------------------------+
```

Every section is 64-byte aligned and protected by an individual CRC32 checksum recorded in the header table.

---

## 2. Inverted Index Compression

- **DocID Delta Encoding**: Postings are grouped into blocks of 128 documents. Within each block, docID deltas are bit-packed using Frame-of-Reference (FOR) bit-packing at bit widths `0..32`.
- **SIMD Unpacking**: Decoders dynamically utilize SSE2 and AVX2 vector instructions for parallel decompression.
- **Term Frequencies & Positions**: Term frequencies are compressed using variable-byte (Varint) encoding. Positions are delta-encoded and packed with Varint.

---

## 3. Query Evaluation & Ranking

- **BM25 Scoring**: Okapi BM25 (`k1 = 0.9`, `b = 0.4`) ranks documents based on term frequency and document length normalization.
- **Block-Max WAND (BMW)**: Dynamically prunes postings lists by tracking upper-bound scores per 128-posting block (`block_max_score`), skipping non-competitive documents before decoding.
- **Boolean & Phrase Evaluation**: Conjunction queries use galloping search over skip blocks. Phrase queries verify exact position adjacencies via delta-decoded position streams. Negated terms (`-term`) filter matching candidates.

---

## 4. Pattern Search & Spell Correction

- **Levenshtein Automata**: Evaluates fuzzy queries and spelling corrections within edit distance $k \in \{1, 2\}$ by traversing the Radix Trie in lockstep.
- **Thompson NFA & DFA Engine**: Regular expressions are compiled to ASTs, converted into Thompson NFAs with bounded state sizes, and lazily determinized for linear-time pattern matching.
- **FM-Index Substring Search**: Burrows-Wheeler Transform (BWT) combined with BitVectors and Wavelet Trees enables arbitrary substring search.
