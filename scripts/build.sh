#!/bin/bash
# ─────────────────────────────────────────────────
# build.sh  —  build the HFT engine
#
# Usage:
#   ./scripts/build.sh            # Release build
#   ./scripts/build.sh debug      # Debug build (ASan + UBSan)
#   ./scripts/build.sh test       # Build + run tests
#   ./scripts/build.sh bench      # Build + run benchmark
# ─────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_TYPE="Release"

if [[ "${1:-}" == "debug" ]]; then
    BUILD_TYPE="Debug"
fi

BUILD_DIR="${ROOT}/build/${BUILD_TYPE}"
mkdir -p "${BUILD_DIR}"

echo "── Configuring (${BUILD_TYPE}) ──────────────────────"
cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "── Building ─────────────────────────────────────────"
cmake --build "${BUILD_DIR}" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

if [[ "${1:-}" == "test" ]]; then
    echo "── Running tests ────────────────────────────────────"
    cd "${BUILD_DIR}" && ctest --output-on-failure
fi

if [[ "${1:-}" == "bench" ]]; then
    echo "── Running benchmark ────────────────────────────────"
    "${BUILD_DIR}/bin/hft_bench"
fi

echo "── Done ─────────────────────────────────────────────"
echo "  Binaries: ${BUILD_DIR}/bin/"
