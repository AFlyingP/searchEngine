# Needlefish System Architecture & Design Document

`needlefish` is a high-performance full-text + substring search engine implemented in modern C++20 from scratch with zero third-party core library dependencies.

---

## 1. Succinct Data Structures (Phase 1)

### 1.1 Rank/Select BitVector (`needlefish::RankSelectBitVector`)
- **Underlying Storage**: 64-bit unsigned words (`uint64_t`).
- **Superblock Directory**:
  - Superblock interval: Every 512 bits (8 `uint64_t` words).
  - Superblock entry:
    - `uint64_t superblock_rank`: Cumulative 1-bit count before the superblock.
    - `uint16_t subblock_ranks[8]`: Relative 1-bit count within the 8 sub-blocks (words).
- **Time Complexity**:
  - `rank1(i)`: $O(1)$ time ($\approx 1.99 \text{ ns}$ hot, 1 superblock lookup + 1 subblock lookup + 1 popcount).
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
- **Phases**:
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

## 2. Directory Layout & Module Organization

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
│   └── fm/                     # FM-Index and Burrows-Wheeler Transform
├── cli/                        # CLI tool (index, search, suggest, stats)
├── tests/                      # Unit & property test suites
│   ├── unit/                   # Deterministic & boundary tests
│   └── property/               # Exhaustive differential oracle property tests
├── bench/                      # Performance benchmarking harness
│   ├── fetch_corpora.sh        # SHA256-verified corpus download utility
│   └── bench_main.cpp          # Google Benchmark suite
└── docs/                       # Design documents & benchmark metrics
    ├── design.md
    └── benchmarks_phase1.md
```
