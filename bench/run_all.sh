#!/usr/bin/env bash
set -euo pipefail

# Needlefish Search Engine - Full Benchmark Suite Runner
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo "=== Building Needlefish Benchmarks ==="
cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build "${BUILD_DIR}" --config Release --target bench_needlefish bench_matrix -j

echo "=== Running Google Benchmark Microbenchmarks ==="
"${BUILD_DIR}/bench/bench_needlefish"

if [ -f "${BUILD_DIR}/wikipedia.idx" ]; then
    echo "=== Running Comprehensive Benchmark Matrix ==="
    "${BUILD_DIR}/bench/bench_matrix" "${BUILD_DIR}/wikipedia.idx"
else
    echo "Wikipedia index not found at ${BUILD_DIR}/wikipedia.idx; skipping matrix benchmark."
fi
