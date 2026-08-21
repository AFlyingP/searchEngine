# Needlefish — Code Review & Bug Review

**Project:** `needlefish` (C++20 search engine) · **Reviewed at:** commit state in `C:\Users\awu93\Downloads\searchEngine-m11ain\searchEngine-main`
**Scope:** full repository, reviewed against the original specification ("build a complete production-quality search engine: SA-IS, FM-index, Levenshtein automata, inverted index + BM25 + block-max WAND, server, demo, benchmarks, packaging").

---

## 0. Methodology

This review combines four kinds of evidence:

1. **Full line-by-line reading** of every source file (`src/**`, `cli/`, `server/`, `web/`, `bindings/`, `worker/`, tests, CI workflows, CMake, docs).
2. **Empirical differential testing.** The core library was compiled directly with GCC 16.2 (MSYS2) and exercised with a purpose-built harness (`review_scratch/harness.cpp`, `review_scratch/sais_probe.cpp`):
   - SA-IS vs. naive suffix-sort oracle on 300 random/adversarial strings **and** ~60 structured inputs (classic `mmiissiissiippii`, Fibonacci strings, de Bruijn sequences B(2,7)/B(3,5)/B(4,4), `(ab)^n` runs with mutations, nested squares).
   - FM-index `count`/`locate` vs. brute-force string scan (60 texts × 30 patterns each).
   - Levenshtein automaton vs. Wagner–Fischer DP: exhaustive `{a,b}^≤5` × k∈{1,2} plus 20 000 random pairs ≤ 40 chars.
   - Porter stemmer idempotency over the committed 23 531-word vocabulary.
   - End-to-end pipeline checks through the real `IndexBuilder → .idx → IndexView → QueryEvaluator` path.
3. **CLI smoke test:** built `needlefish.exe` from source and ran index/search end-to-end.
4. **Strict-flag compile check:** all 22 core `.cpp` files under `-std=c++20 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow`.

Findings marked **[empirically confirmed]** were reproduced by executing the code; the rest are established by code reading.

---

## 1. Executive Summary

The succinct core of this project is genuinely good and mostly *correct*: SA-IS, the FM-index, the rank/select bitvector, and the wavelet tree all passed every differential oracle I could throw at them, including adversarial structured inputs. The BM25 formula is right, the WAND pivot mechanics are sound, the mmap layer is clean RAII, and the CLI works end-to-end.

Everything around that core diverges sharply from the spec:

- **Three remotely or file-triggerable memory-safety / crash bugs** violate the spec's "corrupt input must produce a clean error, never UB" and "5 s timeout" requirements.
- **The headline differentiators are not what they claim to be.** There is no Schulz–Mihov parametric automaton (it's a plain DP-row NFA, mislabeled in code and docs). There is no SIMD posting decode (`unpack128_simd` literally calls the scalar function while README/benchmarks advertise SIMD numbers). "Block-max" scores are a per-*term* constant stamped into every block, not per-block maxima.
- **Query semantics are wrong in user-visible ways**: `-negation` *includes* documents containing the negated term; phrase queries match across removed stopwords; external document IDs are silently discarded; double stemming corrupts ~3 % of English vocabulary queries.
- **Scalability claims are structural fiction**: no segment spilling exists despite the memory-budget API; the whole corpus lives in RAM during indexing.
- **The live-demo deliverable routes users to a hardcoded ephemeral `trycloudflare.com` tunnel**, which is unstable and a security hazard when hostnames recycle.
- **CI quality gates are decorative**: the coverage job can never fail and gates nothing; the strongest test suite (Porter golden vectors) silently skips; TSan never runs; fuzzers are never fuzzed.

| Area | Verdict |
|---|---|
| Succinct structures (bitvector/wavelet/SA-IS/FM) | ✅ Correct, well-tested, hand-implemented as required |
| Analyzer / stemmer | ⚠️ Solid decoder & stemmer; query-path double-stemming bug |
| Inverted index / postings | ⚠️ Correct happy path; corrupt-input UB, fake SIMD, fake block-max, no spill |
| Ranking (BM25/WAND/query eval) | ⚠️ Formula correct; NOT-operator inverted; cap-before-sort fuzzy bug |
| Storage format | ✅/⚠️ Good bounds checks & CRCs; several residual UB holes on crafted input |
| Fuzzy search | ❌ Not the spec'd algorithm (works, but mislabeled + truncated >128 chars) |
| Server | ❌ Remotely crashable, no spec'd protections, broken request framing |
| Web UI | ❌ Stored XSS via unescaped snippets |
| CI / release / packaging | ❌ Gates are fake; release gaps; missing writeup/api docs/man/LICENSE |

---

## 2. Critical Findings

### C-1. Unauthenticated remote crash: any invalid regex kills the whole server
**Files:** `src/server/http_server.cpp:569-571` (no try/catch around per-request work), `src/rank/hybrid_search.cpp:162`, throws at `src/automata/regex.cpp:41,49,126,150,229`

```cpp
HttpResponse resp = handle_request(req);   // http_server.cpp start() loop — no try/catch
...
Regex reg(regex_pattern);                  // hybrid_search.cpp — throws std::runtime_error
```

`GET /api/search?q=)&mode=regex` makes the `Regex` constructor throw; the exception propagates out of `handle_request` through the accept loop and terminates the process. One HTTP request = full DoS of the deployed engine. Aggravated by `start_demo.ps1/.bat`, which publish port 8080 through `cloudflared` with no auth.
**Fix:** wrap request handling in try/catch returning HTTP 400; validate/compile the pattern before scanning; cap pattern length and NFA/DFA state count.

### C-2. Stored XSS: raw document text rendered into `innerHTML`
**Files:** `src/rank/snippet.cpp:92-98` (raw doc bytes wrapped only in `<em>` tags, never HTML-escaped) → JSON transport → `web/app.js:453`: `<p class="result-snippet">${hit.snippet || ''}</p>` assigned to `innerHTML` (same flaw offline at app.js:380-384)

Any indexed document containing `<img src=x onerror=...>` executes JavaScript in every visitor's browser. Titles *are* escaped; snippets are not. The UI also lets users point it at arbitrary backend URLs, so a malicious backend owns the page via unescaped snippets.
**Fix:** HTML-escape document text before inserting highlight markers (server-side preferred; escape-then-highlight client-side too).

### C-3. Out-of-bounds read in `PostingListReader::read_positions` on any truncated/corrupt index
**Files:** `src/invidx/postings.cpp:247-264`; enabling flaw: `Varint::decode_uint32` (`src/invidx/compression.cpp:25-41`) takes **no end pointer**

```cpp
const uint8_t* ptr = positions_data_.data() + decoded_pos_offsets_[block_idx_]; // unvalidated offset
for (uint32_t i = 0; i < count; ++i) {          // count is file-controlled
    ptr += Varint::decode_uint32(ptr, &delta);  // reads up to 5 bytes blindly
```

- A missing `Positions` section ⇒ `nullptr/empty span + header offset` dereference → SIGSEGV.
- A corrupted `positions_offset` (e.g. `0xFFFFFFFF`) ⇒ pointer far outside the mapping.
- A corrupted `pos_count` ⇒ walk past the end of the section.

This directly violates the spec's most emphasized requirement ("index loader … must yield clean errors, never crashes/UB"). Note the block-load walk (`postings.cpp:182-185`) *does* bounds-check — `read_positions` simply doesn't.
**Fix:** add an end-pointer overload of `decode_uint32`; validate the per-doc position offset against `positions_data_.size()` before forming the pointer (the writer already stores `positions_bytes` — currently dead — use it for validation).

### C-4. The "live public demo" depends on an ephemeral tunnel URL hardcoded as fallback
**Files:** `worker/index.js:7`, `wrangler.toml`, `start_demo.ps1`

```js
const backend = env.BACKEND_URL || "https://persistent-mambo-shelf-correctly.trycloudflare.com";
```

`trycloudflare.com` hostnames are free, ephemeral, and recyclable. Deployments without the env var send user queries to whoever holds that hostname and render whatever comes back (also forwards Cookie/Authorization headers wholesale). The README's "Live Web Demo" link therefore points at infrastructure that can vanish or be hijacked. Additionally `wrangler.toml:2` sets `compatibility_date = "2026-08-19"` (in the future), which `wrangler deploy` rejects outright.
**Fix:** fail closed (503) when `BACKEND_URL` is unset; deploy the C++ server itself on a VPS behind HTTPS per the spec; strip credentials when proxying; fix the compatibility date.

---

## 3. Major Findings

### Correctness

**M-1. Boolean NOT is unimplemented — negated terms become positive OR terms** `[empirically confirmed]`
`src/rank/query_eval.hpp:21-25` declares `QueryClause::is_negated` but nothing ever constructs it; `query_eval.cpp:269-281` pushes every token into a disjunction. The analyzer strips `-` as punctuation, so `index -lucene` searches for documents containing *either* term. Harness output:

```
[ ok ] query 'index -lucene' RETURNS the lucene doc (negation treated as OR term)
```

**M-2. External document IDs are stored but never used**
`src/invidx/index_builder.cpp:134,151,166` — postings use internal sequential ids; `DocMetadataRecord::doc_id` is written to disk but no code reads it back. Harness: a corpus line `{"id":77,...}` produces hits with `doc_id == 0`. The CLI smoke test shows it visibly: the document indexed as `"id":2` is reported as `[1] ... DocID: 1`.
**Fix:** translate at the output boundary (CLI/server/C API) via the stored metadata; dedupe/reject duplicate ids at ingest (ids are also silently renumbered when `stoul` fails, builder L192-195).

**M-3. Stopword removal compacts positions → false phrase positives** `[empirically confirmed]`
`src/util/analyzer.cpp:123-131`: `pos++` happens only for kept tokens, so `"quick the brown fox"` indexes identically to `"quick brown fox"` and the phrase check `pos[i+1]==pos[i]+1` (`query_eval.cpp:198-215`) matches both. Every phrase spanning a removed stopword is a false positive ("state of the art" ↔ `"state art"`).

**M-4. Levenshtein target silently truncated to 128 characters** `[empirically confirmed]`
`src/automata/levenshtein.cpp:8`: `target_(target.substr(0, min(size, 128)))`. Harness confirms a candidate at true distance 5 from a 133-char target is *accepted* at k=2 because the tail was cut off. Long-token corpora (URLs, chemical names, base64) produce wrong fuzzy results with no diagnostic.

**M-5. Query-path double stemming corrupts ~3 % of dictionary queries** `[empirically confirmed]`
`analyze()` already stems; `search_disjunction` then calls `normalize_term` (`query_eval.cpp:23`) which stems again. Porter is not idempotent: **785 / 23 531** committed vocabulary words change when stemmed twice (e.g. `abase → abas → aba`, `accidental → accident → accid`). Any such word typed as a query is stemmed past its indexed form and silently returns zero hits. Same pattern in the snippet generator (`snippet.cpp:30`).

**M-6. Fuzzy expansion caps results *before* ranking them** `[empirically confirmed]`
`levenshtein.cpp:62,74` stop collecting at `max_results` during DFS; the distance sort happens afterwards (`:112-116`). With ≥ max_results candidates within distance, the exact-match term (visited last) is dropped entirely. Harness: exact term absent from results while ten distance-2 distractors with higher docFreq fill the quota. Spec requires keeping the *best* candidates.

**M-7. "Block-max" WAND uses a per-term constant, not per-block maxima**
`postings.cpp:65-70` stamps the term-global `max_term_score` (computed once in `index_builder.cpp:223-233`) into *every* block header. Pruning stays sound (global max ≥ any block max) but strictly weaker than documented (`docs/design.md:100`, `docs/architecture.md:58` claim per-block upper bounds and attribute the "7.68× speedup" to them). Related footgun: `PostingListWriter::finish(..., float max_term_score = 0.0f)` defaults to 0, and since `wand.cpp:48` uses strict `>`, an index built through that default yields **zero results for valid queries**.

**M-8. Custom BM25 parameters silently break WAND soundness**
Block-max scores are hardwired to compile-time defaults at build time (`index_builder.cpp:229` calls `score_tf` without arguments), but `QueryEvaluator` accepts an injected scorer (`query_eval.hpp:35`). With `k1=1.5` at query time, true scores can exceed stored block-maxes → top-k results silently wrong. Store k1/b in `Bm25Stats` and validate at load.

**M-9. No segment spill — memory budget is dead code**
Spec §2b requires spilling segments at a memory budget and k-way merging. `memory_budget_` is stored (`index_builder.hpp:59-60`) and never read; `estimated_memory_usage_` (builder L171-173) counts 64 bytes per *term* and ignores positions vectors, map overhead, and the writer buffers (which buffer an entire term's list until `finish()`, `postings.cpp:97-108`). Everything — full corpus text, all postings, all positions — lives in RAM before a single byte is written. GB-scale indexing is impossible within the claimed resource envelope.

**M-10. Hand-written JSONL parser mangles standard JSON** `[empirically confirmed for \u]`
`index_builder.cpp:61-122`:
- `\uXXXX` escapes are destroyed (`\u00e9` → literal `u00e9`; harness: title stored as `Cafu00e9 Test`). Their own extraction script uses `ensure_ascii=False`, but the spec's Phase-0 pipeline (wikiextractor) emits ASCII-escaped JSONL by default → **all non-ASCII text corrupted**.
- Key lookup uses substring `find("id")` anywhere in the line — an `"id"` occurring inside article text earlier than the real field hijacks parsing.
- Closing-quote detection `line[curr]=='"' && line[curr-1]!='\\'` mis-terminates values ending in an escaped backslash.

**M-11. Deserializer holes (crafted-file DoS / UB)**
- `RadixTrie::deserialize` (`radix_trie.cpp:317-343`) trusts 64-bit `num_n`/`pool_sz` → multi-TB `resize` attempt = `bad_alloc` DoS; accepts `num_n==0` producing a rootless trie whose `lookup("x")` (`:139`) indexes `nodes_[0]` of an empty vector → UB. Link/pool validation (L347-367) exists and is good, but cycles are not rejected → infinite DFS/hang.
- `FMIndex::deserialize` accepts `sample_rate == 0` → division by zero later; huge sizes → bad_alloc.
- All deserializers copy through `stringstream` — contradicting the "zero parsing on load / <50 ms load" claims for large indexes.

**M-12. Regex engine: unbounded state explosion + linear full-corpus scan**
`regex.cpp:366-381` unrolls `{n,m}` up to 1000 copies; nested quantifiers multiply (depth limit 64 allows ~10⁹+ states → OOM); eager DFA subset construction (`:414-465`) builds *all* reachable states (exponential for `(a|b){20}c`); `hybrid_search.cpp:166-171` then regex-scans every document's full text per query. Combined with C-1 this is the crashiest surface in the product.

**M-13. HTTP request framing is broken**
`http_server.cpp:564-570` reads exactly one `recv()` (≤4095 bytes) and treats it as the whole request; headers are never parsed and Content-Length ignored (`:246-251`). Any request split across TCP segments — common — is truncated mid-line → spurious 400s/garbage queries.

**M-14. Python ctypes binding: GIL-released calls race with `__del__` → use-after-free**
`bindings/python/needlefish.py:59` (CDLL releases GIL) vs `__del__ → needlefish_close` (`:118-121`). Thread A inside `needlefish_search` while thread B drops the last reference frees the mmap mid-read.

**M-15. Spec'd server protections entirely absent**
No thread pool (single-threaded accept loop), no per-IP token bucket, no 256-char query cap, no 5 s deadline, no gzip, no `/api/health`. One slow client stalls everyone. Additional routing quirks: `~` anywhere reroutes the whole query to fuzzy and truncates at the first tilde (`hybrid_search.cpp:47-61`); `stoul("~ -1")` wraps to `SIZE_MAX` → effectively-unbounded match radius; AND-queries are evaluated as OR (spec says bare terms AND-ed); `*infix*` syntax unsupported (mode flag only); `/api/search` schema uses `limit`/`mode`/`doc_id`/`total_hits` instead of spec'd `k`/`fuzzy`/`id`/`total_estimate`/`corrected`; `/api/suggest` param is `prefix` not `q`.

**M-16. The SIMD posting decoder does not exist**
`compression.cpp:134-136`:

```cpp
void BitPacking::unpack128_simd(...) noexcept {
    unpack128_scalar(in, out, bit_width);   // ← the "SIMD" path
}
```

`NEEDLEFISH_HAS_X86_SIMD` is defined and never used; there is no `_mm_*` intrinsic anywhere (grep-verified). Yet the README ("SIMD-BP128", ">413 Million postings/sec"), design.md (">1.36 Billion postings/s" — note the two docs disagree with each other), and bench/report.md all publish "SIMD" numbers measured on the scalar path. Benchmarks also hardcode the corpus size (271,979) and label 50 trials as "1,000 Sample Trials"; the scalar-vs-SIMD ablation is hardcoded rather than measured, and prints "SIMD …× faster" even when SIMD measured slower (`bench_matrix.cpp:225,230,252,255-259`). This fails the spec's honest-reporting requirement head-on.

### Build / CI / Release

**M-17. Coverage job measures nothing and gates nothing** — `.github/workflows/ci.yml:70-77` swallows every failure with `|| true`, looks for `default.profraw` in the wrong directory, and has no threshold check. The spec's ≥90% libindex gate does not exist.

**M-18. The Porter golden suite silently skips in CI** — `tests/unit/test_analyzer.cpp:51-59` opens `tests/golden/porter/voc.txt` relative to the *build* directory; the files aren't copied there, so `GTEST_SKIP` fires on every CI run. The project's single strongest test never executes. (Related provenance note: the committed `output.txt` corresponds to the *updated* Porter variant — e.g. `possibly→possibl`, `assembly→assembl` — consistent with the implemented `bli→ble`/`logi→log` rules, so design.md's claim of "all 23,531 official published test vectors" is imprecise about which variant's vectors these are.)

**M-19. No TSan job, no clang-tidy enforcement** — the `tsan` preset exists but CI never runs it (spec: "TSan clean … in CI"); only clang-format is checked, and that check omits `include/` and `fuzz/`.

**M-20. Fuzzers exist by name only** — `fuzz/CMakeLists.txt:15-19` gates libFuzzer on `ENABLE_LIBFUZZER`, which is declared nowhere → always standalone mains over 7 fixed inputs, no `add_test`, never run in CI. `fuzz_query_parser.cpp` fuzzes HTTP parsing, not the spec'd query-syntax parser (which doesn't exist as a component); the Regex engine — the crashiest parser — is unfuzzed.

**M-21. Release workflow gaps** — `.github/workflows/release.yml`: no aarch64 build; `-static-libgcc -static-libstdc++` still links glibc dynamically (not the static binaries the spec demands); macOS artifact is single-arch (no `CMAKE_OSX_ARCHITECTURES`); no `gh release` step attaches anything to the tag; GHCR image name uses `${{ github.repository }}` verbatim (uppercase breaks GHCR).

**M-22. Dockerfile CMD uses a nonexistent flag** — `Dockerfile:33` passes `--static /app/web`; `cli/main.cpp:298` parses only `--web-dir|-w`. Works only by coincidence (CWD contains `web/`). Runs as root, no HEALTHCHECK, undocumented `/data/index.idx` mount.

**M-23. Missing deliverables** — no `docs/how-it-works.md` (the spec's blog-quality writeup), no `docs/api.md`, no man page, no install one-liner script, no LICENSE file (README claims MIT), no reproducible one-script benchmark (`bench/run_all.sh` absent), no competitor runs (Tantivy/MeiliSearch/SymSpell), `bench/report.md` numbers are not regenerable from anything in-tree. A 1.6 MB corpus snapshot (`web/wiki_corpus.json`) is committed despite the "nothing large in git" rule.

---

## 4. Minor Findings (selected)

| # | Where | Issue |
|---|---|---|
| 1 | `postings.cpp:162-168` | Truncated postings fabricate `tf=1` and empty positions silently instead of erroring |
| 2 | `postings.cpp:174-191` | Block load eagerly re-walks the entire positions varint stream to compute offsets the writer could have stored (`positions_bytes` field is dead) — makes skips O(positions) |
| 3 | `postings.cpp:8-15` | `add_posting` accepts non-monotonic docIDs → unsigned delta wrap corrupts index silently |
| 4 | `wand.cpp` / `query_eval.cpp` | Missing `<unordered_map>` / `<algorithm>` includes (transitive-include luck); WAND-vs-naive float accumulation order differs → near-tie ordering can disagree between paths (property test compares exact sequences — latent flake) |
| 5 | `query_eval.cpp:127-133` | Conjunction/phrase heap pop order has no tie-break → nondeterministic order for equal scores (disjunction path is deterministic) |
| 6 | `query_eval.cpp:21-31` | Duplicate query terms scored twice (no dedupe) |
| 7 | `index_file.cpp:69-75` | `checksum != 0` guard lets a flipped table entry disable integrity checking; `header.file_size` never validated; duplicate section IDs accepted |
| 8 | `index_file.cpp:84-88` | `reinterpret_cast<const DocMetadataRecord*>` assumes 8-byte alignment — holds only because sections are written aligned; `open_from_bytes` performs no alignment check. Format is native-endian/ABI-dependent (padding, float repr) — not portable |
| 9 | `snippet.cpp:20-24,103-105` | Snippet truncation can split UTF-8 sequences (invalid output) |
| 10 | `http_server.hpp:88` | `is_running_` is a plain bool written by `stop()` and read by `start()` — data race; POSIX `close()` doesn't wake blocked `accept()` |
| 11 | `http_server.cpp:544-548` | Transient accept errors (EMFILE) spin at 100 % CPU |
| 12 | `http_server.cpp:508` | `inet_pton` failure ignored → invalid `--host` silently binds 0.0.0.0 |
| 13 | `http_server.cpp:49-68` | URL-decode applies `+`→space to paths; `%0x`-style inputs accepted via `istringstream >> std::hex` |
| 14 | `http_server.cpp:338` | NaN/inf scores serialize as invalid JSON (UI falls back to offline mode silently) |
| 15 | `web/app.js:244-299` | Debounce ✓, latency badge ✓, dark mode ✓ — but no AbortController: stale responses can overwrite newer results (spec requires aborting in-flight fetches) |
| 16 | `http_server.cpp:138-140` | CORS `*` on every response |
| 17 | `c_api/needlefish_c.cpp:46-58` | `needlefish_close`/stats getters not exception-guarded (a throw through `extern "C"` is UB); lifetimes/thread-safety undocumented |
| 18 | `cli/main.cpp:104,108,198,201,297` | `stoul` on numeric args: negatives wrap (`--k -1` → SIZE_MAX), `--port 99999` truncates via uint16 cast |
| 19 | `bench_main.cpp:112-128,155-177` | `BM_FMIndex_Locate` and `BM_Posting_Decode_SIMD` never registered via `BENCHMARK(...)` — 2 of 9 benchmarks don't run |
| 20 | `sais.hpp:25,77` | Stale contract comment ("input must have sentinel at s[n-1]" — the implementation appends it itself); "≤ 6n bytes" memory claim is false (measured working set ≈ 21n+ bytes in 32-bit mode: t(n)+sa(4n)+lms(4n)+names(4n)+sorted(4n)+s1(4n)) |
| 21 | `README.md:43` vs `store/index_file.hpp:18` | Magic stated as `NFI\x01` in README, actual is `NFLSHIDX` (design.md agrees with code; README wrong) |
| 22 | `mmap.cpp:84-87` | Empty file maps to `data_=nullptr` with `is_open()==false` — indistinguishable from failure downstream |
| 23 | `radix_trie.cpp:59,87,125` | `edge_len` uint16 silently truncates keys ≥ 65 536 bytes |
| 24 | `index_builder.cpp:241,44` | `postings_bytes`/`positions_offset` narrow size_t→uint32_t (>4 GB streams wrap silently) |
| 25 | `index_builder.cpp:319-345` | `write_index` ignores ostream write results — ENOSPC yields a silently truncated index reported as success |
| 26 | Tests | `test_cli_e2e.cpp` never spawns the CLI and skips FM assertions via `if (fm_index())`; `<50 ms` wall-clock assertion is flaky under sanitizers; no coverage of C API/Python binding/socket layer |

---

## 5. What Was Verified Correct (credit where due)

These were checked line-by-line **and** differentially tested where feasible:

- **SA-IS** (`src/sa/sais.cpp`): correct against naive oracle on all ~360 inputs tested — random, all-equal, `(ab)^n`, Fibonacci, de Bruijn B(2,7)/B(3,5)/B(4,4), nested squares, long runs, and the classic `mmiissiissiippii`. Induced sorting, LMS naming/equality, recursion condition, and final induction are all faithful. (An initial suspicion on the classic example was my own manual-sort error — the computed oracle confirms the implementation.)
- **FM-index** (`src/fm/fm_index.cpp`): BWT construction incl. sentinel row handling, C-table, backward search, LF-walk locate with sampled rows, and `extract()` anchor logic all verified correct (60 texts × 30 patterns vs brute force, including absent patterns).
- **Rank/select bitvector**: rank9-style directory, BMI2 `_pdep` select with correct portable fallback, select0 with partial trailing words (phantom-bit edge cases traced by hand — sound), sentinel superblock in binary search.
- **Wavelet tree**: stable-partition construction, node-offset tables, access/rank/select descent math verified symbol-by-symbol; deserialize rebuilds offsets deterministically.
- **UTF-8 decoder**: overlong/surrogate/out-of-range rejection all correct; never crashes on arbitrary input (fuzz-style probing by inspection).
- **Porter stemmer**: faithful implementation of Martin Porter's *updated* variant; self-consistent with the committed goldens.
- **BM25**: formula, IDF floor (`log(1+…)`), k1=0.9/b=0.4, avgdl divide-by-zero guard — all correct.
- **WAND pivot/tie-break/termination** logic is internally consistent (weakness is the block-max data, M-7, not the algorithm).
- **RadixTrie insert/split sibling relinking**, prefix mid-edge descent, and deserialize-time link/pool validation.
- **MemoryMappedFile**: correct Win32/POSIX RAII, move semantics, error cleanup ordering.
- **Loader section-bounds arithmetic** (`offset/length` overflow-free checks) is right.
- **Strict warnings**: 21/22 core files compile clean under `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow` on GCC 16 (the 22nd, `needlefish_c.cpp`, only trips a dllimport artifact of compiling outside its CMake target).
- **CLI end-to-end works**: JSONL → `.idx` → BM25 search with highlighted snippets, sub-ms latency on tiny corpora.
- The existing test suite contains genuine differential/property tests (SA-IS oracle, DFA vs DP, WAND-vs-naive equivalence, corruption regressions, golden queries) — the culture is right even where coverage gates are fake.

---

## 6. Spec Compliance Scorecard

| Spec item | Status |
|---|---|
| Dependency-free C++20 libindex | ✅ standard library only, hand-implemented structures |
| CMake presets debug/release(LTO)/sanitize/tsan/bench | ✅ present; tsan unused by CI ⚠️ |
| Warning-free GCC≥12/Clang≥15 `-Wall…-Werror` | ✅ verified on GCC 16 (core) |
| Rank/select bitvector, wavelet tree, SA-IS, FM-index | ✅ correct |
| Streaming builder w/ segment spill @ memory budget | ❌ no spill; budget dead code (M-9) |
| Postings: 128-block FOR/PFOR, SSE/AVX2 + portable | ⚠️ bit-packing correct; **SIMD path is a stub** (M-16); "PFOR" is plain FOR |
| Per-block skip pointers w/ block-max score | ⚠️ structure present; block-max is per-term constant (M-7) |
| BM25 k1=0.9 b=0.4, galloping AND, OR, NOT, phrase | ⚠️ BM25/OR/phrase ✅; **AND is actually OR-evaluated for bare terms; NOT missing** (M-1) |
| Term dict: flattened trie, prefix range, DFA lockstep | ✅ |
| Schulz–Mihov parametric automata (k=1,2), O(\|q\|) DFA | ❌ **not implemented** — DP-row NFA instead; docs/README mislabel it; correctness otherwise fine, 128-char truncation bug (M-4) |
| Substring via FM over term-dictionary string | ⚠️ implemented as stretch variant (FM over full doc text) instead; unbounded `locate` = DoS on frequent substrings |
| `.idx` format, magic/version/checksums, mmap, corrupt→clean error | ⚠️ mostly good bounds checks; C-3/M-11 holes remain |
| Golden query suite (20 queries, byte-stable) | ✅ exists (unit/golden) |
| Load < 50 ms regardless of size | ❌ TermDict/FM fully deserialized (copies) at open |
| Server: thread pool, 5 s timeout, 256-char cap, rate limit 10 QPS, gzip, /api/health | ❌ none present (M-15) |
| Query syntax: AND, phrases, `-negation`, `*infix*`, `~N` | ❌ AND→OR, negation inverted, infix via mode only, `~N` global not per-term |
| Web UI: debounce 80 ms, abort fetches, badge, dark mode | ⚠️ debounce/badge/dark ✅; abort ❌; XSS ❌ (C-2) |
| Live demo, full Wikipedia, HTTPS, real domain | ❌ ephemeral trycloudflare tunnel via Worker (C-4); SimpleWiki (272 k docs), not full English Wikipedia |
| Reproducible benchmarks (one script) vs Tantivy/MeiliSearch | ❌ no run_all.sh, hardcoded report sections, no competitors, fabricated labels (M-16/M-23) |
| CI matrix gcc/clang × debug/release/sanitize + format + coverage ≥90% gate + TSan | ⚠️ matrix ✅, sanitize genuinely wired ✅; coverage fake (M-17), TSan missing (M-19), tidy unenforced |
| release.yml static x86_64+aarch64+macOS → GitHub Release + GHCR | ⚠️ builds exist; arch/static/attach/GHCR-case gaps (M-21) |
| Fuzzers ≥ 24 CPU-hours, findings fixed & documented | ❌ inert standalone mains, never run (M-20) |
| Writeup how-it-works.md, api.md, man page, install.sh, LICENSE | ❌ all missing (M-23) |
| v1.0.0 tag w/ prebuilt binaries, Docker image, install one-liner | ❌ |

---

## 7. Prioritized Fix List

**P0 — security/stability (before any deployment):**
1. Try/catch at the request boundary + regex pattern/state caps (C-1, M-12).
2. HTML-escape snippets end-to-end (C-2).
3. Bounds-checked varint decoding + position-offset validation in `read_positions` (C-3); reject rootless tries and absurd allocation sizes in deserializers (M-11); validate `sample_rate != 0`.
4. Fail-closed worker backend; fix future `compatibility_date` (C-4).

**P1 — result correctness:**
5. Implement NOT (or remove the syntax); evaluate bare terms as AND per spec or update docs (M-1, M-15).
6. Single-stem the query path (drop the second `normalize_term` stem or make stemming idempotence-safe) (M-5).
7. Pre-stopword position assignment (one-line analyzer fix, fixes both sides consistently) (M-3).
8. Return external doc IDs at output boundaries (M-2).
9. Collect-all-then-sort (or bounded top-k by distance) in `match_trie`; raise/remove the 128-char automaton cap (M-6, M-4).
10. Compute real per-block maxima; remove `finish()`'s 0.0f default; store k1/b in stats and validate at load (M-7, M-8).

**P2 — honesty of claims:**
11. Either implement the SIMD unpack and parametric automata, or correct README/design/bench language and re-measure; regenerate bench numbers via a committed script; delete fabricated ablations (M-16).
12. Implement segment spill or remove the budget API and scale claims (M-9).
13. Replace the hand JSON parser (proper string scanning with escape-state machine, `\uXXXX` decoding) or vendor an allowed parser for the ingest layer (M-10).

**P3 — engineering infrastructure:**
14. Make the coverage job real (`LLVM_PROFILE_FILE`, `llvm-cov report --failure-threshold=90`, no `|| true`); add TSan job; run Porter goldens regardless of build dir; enforce clang-tidy; wire libFuzzer builds + short CI fuzz runs + regression corpus (M-17-M-20).
15. Fix release workflow (aarch64, true static or musl, universal macOS, attach artifacts, lowercase GHCR) and Dockerfile flags (M-21, M-22).
16. Rebuild the server read path per spec (thread pool, recv loop + header parse, timeouts, caps, token bucket, gzip, /api/health) (M-13, M-14, M-15).
17. Produce the missing deliverables: how-it-works.md, api.md, man page, install.sh, LICENSE, run_all.sh (M-23).

---

## 8. Suggested Next Steps for Verification

After applying P0/P1 fixes, the fastest regression net is to promote `review_scratch/harness.cpp` + `review_scratch/sais_probe.cpp` into the real test suite (they need only the dependency-free core), and to point the existing property tests' expectations at computed oracles rather than hardcoded literals (the same mistake that briefly made the classic SA-IS test look failing here).

---

*Review artifacts: `review_scratch/harness.cpp` and `review_scratch/sais_probe.cpp` (the empirical harnesses used; build objects/binaries were removed after the review). All empirical outputs quoted above were produced in this environment (Windows, GCC 16.2.0, `-O1` builds) by linking these harnesses against the unmodified library sources.*
