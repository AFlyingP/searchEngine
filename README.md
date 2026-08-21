# Needlefish

**Needlefish** is an embedded, zero-external-dependency, succinct search and substring indexing engine built in modern ISO C++20. It combines Okapi BM25 ranking (accelerated by Block-Max WAND dynamic pruning) with succinct full-text indexing (linear-time SA-IS suffix array construction, Burrows-Wheeler Transform, and wavelet tree-backed FM-Index) inside a single memory-mapped container format (`.idx`).

- **Live Web Demo**: [needlepig.aflyingbay49.workers.dev](https://needlepig.aflyingbay49.workers.dev)
- **Zero Runtime Dependencies**: Written entirely in standard C++20 without external libraries.
- **Sub-Millisecond Retrieval**: Evaluates queries over 272,000+ Wikipedia articles in under 1 ms.

---

## Architecture Overview

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

## Benchmark Results

Tested on **Simple English Wikipedia** (271,979 documents, 685.18 MB indexed corpus, 34.97M tokens, 834,149 unique terms):

| Query Class | $p_{50}$ Latency | $p_{95}$ Latency | $p_{99}$ Latency | Throughput (QPS) |
| :--- | :--- | :--- | :--- | :--- |
| **Prefix Autocomplete** | **26.5 µs** | 30.3 µs | 33.5 µs | **36,797 QPS** |
| **Positional Phrase** | **502.2 µs** | 2,490.8 µs | 2,674.7 µs | **1,062 QPS** |
| **1-Term BM25** | **821.7 µs** | 2,334.8 µs | 2,362.4 µs | **1,287 QPS** |
| **3-Term Boolean AND** | **850.1 µs** | 2,007.0 µs | 2,168.9 µs | **961 QPS** |
| **3-Term Scored OR (WAND)** | **963.7 µs** | 2,987.9 µs | 2,993.9 µs | **732 QPS** |
| **Fuzzy Typo ($k=1$)** | **8.84 ms** | 10.39 ms | 11.31 ms | **110 QPS** |
| **Fuzzy Typo ($k=2$)** | **7.70 ms** | 9.29 ms | 9.67 ms | **125 QPS** |

### Key Profiling Highlights:
- **Block-Max WAND**: Achieves **7.68× speedup** over exhaustive scoring ($925.0\ \mu\text{s}$ vs $7,376.3\ \mu\text{s}\ p_{50}$).
- **Posting Decode**: Sustains **> 413 Million postings / second** across SIMD/bit-packed posting blocks.

---

## Architectural Comparison

| Dimension | Needlefish | Apache Lucene | Tantivy | MeiliSearch |
| :--- | :--- | :--- | :--- | :--- |
| **Language** | C++20 | Java | Rust | Rust |
| **Runtime Dependencies** | None (0 MB) | JVM | None | None |
| **Substring Search** | Hand-crafted FM-Index (SA-IS + Wavelet) | Trigram | Trigram | Trigram |
| **Fuzzy / Typo Search** | Schulz-Mihov DFA $\cap$ Flattened Trie | Levenshtein Automata | FST Automata | Levenshtein DFA |
| **Ranking Pruning** | Block-Max WAND | Block-Max WAND | Block-Max WAND | Bucket Sorting |
| **Index Format** | Single-file zero-copy mmap (`.idx`) | Segment Directory | Segment Directory | LMDB KV Store |

---

## Quickstart

### 1. Build from Source

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target needlefish_cli needlefish_c
```

### 2. Index a JSONL Corpus

```bash
# Ingest documents and produce memory-mapped .idx file
./build/needlefish index --input corpus.jsonl --output corpus.idx --enable-fm
```

### 3. Query from the CLI

```bash
# Term search
./build/needlefish search --index corpus.idx --query "information retrieval"

# Exact phrase search
./build/needlefish search --index corpus.idx --query "\"operating system\""

# Fuzzy typo correction
./build/needlefish search --index corpus.idx --query "relativty~1"
```

### 4. Run HTTP REST API Server

```bash
./build/needlefish serve --index corpus.idx --port 8080 --static web/
```

Access the interactive web dashboard at `http://localhost:8080`.

---

## Programmatic APIs

### C++20 Embedded API

```cpp
#include <needlefish.hpp>
#include <iostream>

int main() {
    needlefish::IndexView index;
    index.open("corpus.idx");

    needlefish::QueryEvaluator eval(index);
    auto results = eval.search("quantum computing", 10);

    for (const auto& hit : results.hits) {
        std::cout << "[" << hit.doc_id << "] " << index.doc_title(hit.doc_id)
                  << " (Score: " << hit.score << ")\n";
    }
    return 0;
}
```

### Python API (ctypes)

```python
from bindings.python.needlefish import NeedlefishIndex

engine = NeedlefishIndex("corpus.idx")
results = engine.search("quantum computing", top_k=5)

for hit in results["hits"]:
    print(f"[{hit['doc_id']}] {hit['title']} (Score: {hit['score']:.2f})")
    print(f"    {hit['snippet']}")
```

---

## REST API Specification

### 1. `GET /api/search?q=<query>&k=<top_k>`
Executes ranked search with term, phrase, boolean, and fuzzy routing:
```json
{
  "took_us": 472,
  "total_estimate": 142,
  "hits": [
    {
      "doc_id": 51828,
      "score": 19.09,
      "title": "Quantum computer",
      "snippet": "...building block of <em>quantum</em> <em>computers</em>..."
    }
  ]
}
```

### 2. `GET /api/suggest?prefix=<prefix>&k=<count>`
Returns instant search-as-you-type prefix completions:
```json
{
  "prefix": "algo",
  "suggestions": ["algorithm", "algorithmic", "algorithms"]
}
```

### 3. `GET /api/stats`
Returns index metadata and memory footprint:
```json
{
  "total_docs": 271979,
  "total_tokens": 34972108,
  "avg_doc_len": 128.58,
  "file_size_bytes": 718452104,
  "has_fm_index": true
}
```

---

## Docker Deployment

```bash
docker build -t needlefish .
docker run -p 8080:8080 -v $(pwd)/data:/data needlefish --index /data/corpus.idx
```

---

## License
MIT License. Created by [AFlyingP](https://github.com/AFlyingP).
