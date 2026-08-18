#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORPORA_DIR="${SCRIPT_DIR}/corpora"
mkdir -p "${CORPORA_DIR}"

echo "========================================================"
echo " Needlefish Benchmark Corpora Fetch & Verify Utility"
echo "========================================================"

verify_sha256() {
    local file_path="$1"
    local expected_hash="$2"
    
    if [ ! -f "${file_path}" ]; then
        return 1
    fi

    local actual_hash
    if command -v sha256sum >/dev/null 2>&1; then
        actual_hash=$(sha256sum "${file_path}" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        actual_hash=$(shasum -a 256 "${file_path}" | awk '{print $1}')
    else
        echo "Warning: Neither sha256sum nor shasum found; skipping SHA-256 check for ${file_path}"
        return 0
    fi

    if [ "${actual_hash}" != "${expected_hash}" ]; then
        echo "SHA256 mismatch for ${file_path}!"
        echo "  Expected: ${expected_hash}"
        echo "  Actual:   ${actual_hash}"
        return 1
    fi
    echo "SHA256 verified for $(basename "${file_path}")"
    return 0
}

# 1. English Word Frequency List (~1M words / 333k unigrams)
WORD_LIST_URL="https://norvig.com/ngrams/count_1w.txt"
WORD_LIST_FILE="${CORPORA_DIR}/word_frequencies.txt"
WORD_LIST_SHA256="51df159fd3de12b20e403c108f526e96dbd723d9cabdd5f17955cdc16059e690"

echo "Checking word frequency list..."
if ! verify_sha256 "${WORD_LIST_FILE}" "${WORD_LIST_SHA256}"; then
    echo "Downloading word frequency list from ${WORD_LIST_URL}..."
    curl -fsSL "${WORD_LIST_URL}" -o "${WORD_LIST_FILE}"
    verify_sha256 "${WORD_LIST_FILE}" "${WORD_LIST_SHA256}"
fi

# 2. Linux Kernel Sample / Archive
# We fetch a stable release tarball or mini corpus for reproducible indexing benchmarks
LINUX_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.1.tar.xz"
LINUX_FILE="${CORPORA_DIR}/linux-6.1.tar.xz"
# Note: For full indexing test, user can run this on clean machine
echo "Linux kernel archive target: ${LINUX_FILE}"
if [ "${FETCH_LINUX_FULL:-0}" = "1" ]; then
    if [ ! -f "${LINUX_FILE}" ]; then
        echo "Downloading Linux kernel source archive..."
        curl -fsSL "${LINUX_URL}" -o "${LINUX_FILE}"
    fi
fi

# 3. Wikipedia Sample JSONL Generator
# For automated lightweight CI and local testing, generate a sample JSONL corpus if not existing
WIKI_SAMPLE="${CORPORA_DIR}/wikipedia_sample.jsonl"
if [ ! -f "${WIKI_SAMPLE}" ]; then
    echo "Generating standard synthetic JSONL benchmark corpus..."
    cat << 'EOF' > "${WIKI_SAMPLE}"
{"id": 1, "title": "Information Retrieval", "text": "Information retrieval (IR) in computer science is the process of obtaining information resources relevant to an information need from a collection of those resources. Searches can be based on full-text or other content-based indexing."}
{"id": 2, "title": "Suffix Array", "text": "In computer science, a suffix array is a sorted array of all suffixes of a string. It is a data structure used, among others, in full-text indices, data compression algorithms, and the field of bioinformatics."}
{"id": 3, "title": "Burrows-Wheeler Transform", "text": "The Burrows-Wheeler transform (BWT) rearranges a character string into runs of similar characters. This is useful for compression, since it tends to be easy to compress a string that has runs of repeated characters by techniques such as move-to-front transform and run-length encoding."}
{"id": 4, "title": "FM-Index", "text": "An FM-index is a compressed full-text substring index based on the Burrows-Wheeler transform with similarities to the suffix array. It allows fast substring searches in compressed text."}
{"id": 5, "title": "Levenshtein Automaton", "text": "A Levenshtein automaton is a finite-state automaton that recognizes the set of all words within a given edit distance of a fixed target word."}
{"id": 6, "title": "BM25", "text": "In information retrieval, Okapi BM25 is a ranking function used by search engines to estimate the relevance of documents to a given search query."}
EOF
fi

echo "========================================================"
echo " Corpora ready in ${CORPORA_DIR}"
echo "========================================================"
