# needlefish

[![CI](https://github.com/AFlyingP/searchEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/AFlyingP/searchEngine/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**needlefish** is an ultra-fast full-text and succinct search engine implemented from scratch in modern **C++20** with **zero external dependencies** in the core indexing and ranking library.

It combines state-of-the-art information retrieval algorithms (Okapi BM25, Block-Max WAND dynamic pruning, Frame-of-Reference posting compression) with compressed succinct data structures (linear-time SA-IS Suffix Array, Byte Wavelet Trees, and FM-Indexes).

---

## Performance Highlights

| Benchmark | Latency / Op | Throughput | Description |
| :--- | :--- | :--- | :--- |
| **`Posting Decode (FOR)`** | **0.73 ns / posting** | **1.365 Billion postings/s** | Frame-Of-Reference 128-docID delta decompression |
| **`BitVector Rank1`** | **1.91 ns** | **525.1 Million ops/s** | 512-bit superblock $O(1)$ popcount rank |
| **`BitVector Select1`** | **49.7 ns** | **20.1 Million ops/s** | Binary search + BMI2 `_pdep_u64` bit scan |
| **`FM-Index Search`** | **921 ns** | **1.086 Million queries/s** | 10-character exact backward search count |
| **`Regex Scan`** | **1.24 ms / 100KB** | **76.59 MB/s** | Linear-time non-backtracking Powerset DFA scan |
| **`Levenshtein Intersect`**| **2.71 ms** | **368 queries/s** | Parametric Levenshtein DFA over 50,000-term Radix Trie |
| **`Index Load Latency`** | **< 10 ms** | **Instantaneous** | Memory-mapped zero-copy section binding |

*Benchmarks measured on x86-64 with `-O3 -march=native` using Google Benchmark.*

---

## Key Features

* **Zero Third-Party Core Dependencies**: Core library (`libneedlefish_core`) relies exclusively on the C++20 standard library.
* **Succinct Data Structures**:
  * **Rank/Select BitVector**: 512-bit superblocks with 64-bit sub-blocks and BMI2 acceleration.
  * **Byte Wavelet Tree**: 8-level bit-sliced balanced binary tree over byte alphabets for $O(1)$ `access`, `rank`, and `select`.
  * **SA-IS Suffix Array**: Linear-time $O(n)$ induced sorting following Nong, Zhang, Chan (2009).
  * **FM-Index & BWT**: Burrows-Wheeler Transform, C-table, Wavelet Occ table, and sampled suffix array locate queries.
* **Inverted Index & Compression**:
  * **128-docID Blocks**: Delta-encoded Frame-of-Reference (FOR) bit packing delivering **> 1.36B postings/sec**.
  * **Variable-Byte Streams**: 7-bit continuation VByte for term frequencies and position streams.
* **Contiguous Flat Radix Trie**:
  * Array-indexed 32-byte nodes with zero heap pointer chasing for $O(m)$ term lookup and search-as-you-type prefix search.
* **Query Evaluation & Ranking**:
  * **Okapi BM25**: Scored ranking with configurable $k_1 = 0.9, b = 0.4$.
  * **Block-Max WAND**: Dynamic pruning skips non-competitive document blocks without decoding posting deltas.
  * **Boolean & Phrase Search**: Galloping intersection (`AND`), scored disjunction (`OR`), and consecutive token positions (`"phrase"`).
* **Automata & Substring Engine**:
  * **Non-Backtracking Regex**: Linear-time $O(n)$ matching via on-the-fly Powerset DFA with a 256-entry transition table per state.
  * **Universal Levenshtein DFA**: Schulz & Mihov (2002) parametric automaton intersecting the contiguous Radix Trie in lockstep ($< 3 \text{ ms}$ over 50,000+ terms).
  * **Search-As-You-Type Autocomplete**: Fast prefix completions and distance-weighted typo suggestions.
* **Hybrid Query Engine**:
  * Seamlessly routes natural language queries to BM25/WAND, typo queries (`term~2`) to Levenshtein automata, regex (`/pattern/`) to the DFA scanner, and raw substrings to the FM-Index.
* **Embedded HTTP REST Server & Web UI**:
  * Built-in zero-dependency HTTP server and modern dark/light web UI dashboard.
* **Zero-Copy Memory-Mapped Storage**:
  * Single `.idx` binary file format with 64-byte aligned sections and CRC-32 integrity validation.
  * Instantaneous load time ($< 10 \text{ ms}$) via Windows `CreateFileMapping` / POSIX `mmap`.

---

## Quick Start

### Prerequisites
* A C++20 compliant compiler: GCC 13+, Clang 16+, or MSVC 2022+
* CMake 3.20+
* Ninja (recommended)

### Build
```bash
# Configure debug build
cmake --preset debug

# Build the CLI tool and test suite
cmake --build --preset debug --target needlefish_cli
cmake --build --preset debug --target test_needlefish_unit
```

### Run Tests
```bash
# Run unit & golden tests (45 tests including 23,531-word Porter golden suite)
./build/debug/tests/test_needlefish_unit.exe

# Run differential property tests (5,000 Levenshtein DFA vs DP matrix trials & 1,000 WAND trials)
./build/debug/tests/test_needlefish_property.exe
```

---

## CLI Usage

### 1. Build an Index from JSONL
Create or provide a `.jsonl` document file (format: `{"id": 1, "title": "...", "text": "..."}`):
```bash
# Build index with full-text + FM-Index substring indexing
./build/debug/needlefish.exe index --input corpus.jsonl --output corpus.idx --enable-substring
```

### 2. Launch the Web UI & REST API Server
```bash
./build/debug/needlefish.exe serve --index corpus.idx --port 8080 --web-dir web
```
Open `http://localhost:8080` in any browser to use the search UI.

### 3. Search the Index via CLI
```bash
# Multi-term scored BM25 query with WAND pruning
./build/debug/needlefish.exe search --index corpus.idx --query "information retrieval systems"

# Exact positional phrase search
./build/debug/needlefish.exe search --index corpus.idx --query '"succinct data structures"'

# Fuzzy typo search (edit distance up to 2)
./build/debug/needlefish.exe search --index corpus.idx --query "sukcinct~2"

# Regular expression search
./build/debug/needlefish.exe search --index corpus.idx --query "/C\+\+\d+/"
```

### 4. Autocomplete & Suggestions
```bash
# Prefix autocomplete
./build/debug/needlefish.exe suggest --index corpus.idx --query "infor"

# Fuzzy typo suggestions
./build/debug/needlefish.exe suggest --index corpus.idx --query "sukcinct" --fuzzy
```

### 5. Display Index Statistics
```bash
./build/debug/needlefish.exe stats --index corpus.idx
```

---

## 📁 Repository Structure

```
.
├── CMakeLists.txt              # Root build configuration (C++20, strict warnings -Werror)
├── CMakePresets.json           # Presets (debug, release, sanitize, tsan, bench)
├── src/                        # Core search engine library (zero external dependencies)
│   ├── bitvector/              # 512-bit superblock Rank/Select BitVector
│   ├── wavelet/                # 8-level byte Wavelet Tree
│   ├── sa/                     # SA-IS linear-time Suffix Array
│   ├── fm/                     # FM-Index and Burrows-Wheeler Transform
│   ├── util/                   # UTF-8 validator, Unicode tokenizer, Porter stemmer
│   ├── invidx/                 # Posting lists, Frame-of-Reference (FOR), Radix Trie, Builder
│   ├── store/                  # Memory-mapped zero-copy .idx file storage
│   ├── automata/               # Non-backtracking Regex, Levenshtein DFA, Autocomplete
│   └── rank/                   # BM25 scorer, Block-Max WAND, Query evaluator, Hybrid search
├── cli/                        # Standalone CLI binary (index, search, suggest, stats)
├── tests/                      # Unit & property test suites
│   ├── unit/                   # Deterministic & boundary tests
│   ├── property/               # Differential oracle property tests
│   └── golden/                 # Official Porter 23,531-word test fixtures
├── bench/                      # Google Benchmark suite
└── docs/                       # Architecture specifications & benchmark tables
    ├── design.md
    └── benchmarks.md
```

---

## 📜 License
This project is licensed under the [MIT License](LICENSE).
