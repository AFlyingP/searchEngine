#!/usr/bin/env bash
set -euo pipefail

# Needlefish Installation Script
PREFIX="${1:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo "=== Installing Needlefish to ${PREFIX} ==="
cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${PREFIX}"
cmake --build "${BUILD_DIR}" --config Release -j
cmake --install "${BUILD_DIR}"

if [ -f "${ROOT_DIR}/docs/needlefish.1" ]; then
    MAN_DIR="${PREFIX}/share/man/man1"
    mkdir -p "${MAN_DIR}"
    cp "${ROOT_DIR}/docs/needlefish.1" "${MAN_DIR}/needlefish.1"
    echo "Installed man page to ${MAN_DIR}/needlefish.1"
fi

echo "Needlefish installed successfully!"
