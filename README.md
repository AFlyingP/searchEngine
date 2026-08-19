# needlefish 🐟

[![CI](https://github.com/AFlyingP/searchEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/AFlyingP/searchEngine/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**needlefish** is an ultra-fast full-text and succinct search engine implemented from scratch in modern **C++20** with **zero external dependencies** in the core indexing and ranking library.

It combines state-of-the-art information retrieval algorithms (Okapi BM25, Block-Max WAND dynamic pruning, Frame-of-Reference posting compression) with compressed succinct data structures (linear-time SA-IS Suffix Array, Byte Wavelet Trees, and FM-Indexes).

---

## ⚡ Performance Highlights

| Benchmark | Latency / Op | Throughput | Description |
| :--- | :--- | :--- | :--- |
| **`Posting Decode (FOR)`** | **0.73 ns / posting** | **1.365 Billion postings/s** | Frame-Of-Reference 128-docID delta decompression |
| **`BitVector Rank1`** | **1.91 ns** | **525.1 Million ops/s** | 512-bit superblock $O(1)$ popcount rank |
| **`BitVector Select1`** | **49.7 ns** | **20.1 Million ops/s** | Binary search + BMI2 `_pdep_u64` bit scan |
| **`FM-Index Search`** | **921 ns** | **1.086 Million queries/s** | 10-character exact backward search count |
| **`Index Load Latency`** | **< 10 ms** | **Instantaneous** | Memory-mapped zero-copy section binding |

*Benchmarks measured on x86-64 with `-O3 -march=native` using Google Benchmark.*

---

## 🌟 Key Features

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
* **Zero-Copy Memory-Mapped Storage**:
  * Single `.idx` binary file format with 64-byte aligned sections and CRC-32 integrity validation.
  * Instantaneous load time ($< 10 \text{ ms}$) via Windows `CreateFileMapping` / POSIX `mmap`.

---

## 🚀 Quick Start

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
# Run unit & golden tests (including official 23,531-word Porter golden suite)
./build/debug/tests/test_needlefish_unit.exe

# Run differential property tests (1,000 WAND vs naive ranking oracle trials)
./build/debug/tests/test_needlefish_property.exe
```

---

## 💻 CLI Usage

### 1. Build an Index from JSONL
Create or provide a `.jsonl` document file (format: `{"id": 1, "title": "...", "text": "..."}`):
```bash
./build/debug/needlefish.exe index --input corpus.jsonl --output corpus.idx
```

### 2. Search the Index
```bash
# Multi-term scored BM25 query with WAND pruning
./build/debug/needlefish.exe search --index corpus.idx --query "information retrieval systems"

# Exact positional phrase search
./build/debug/needlefish.exe search --index corpus.idx --query '"succinct data structures"'
```

### 3. Display Index Statistics
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
│   └── rank/                   # BM25 scorer, Block-Max WAND, Query evaluator, Snippets
├── cli/                        # Standalone CLI binary (index, search, stats)
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
