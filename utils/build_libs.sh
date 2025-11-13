#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/release}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DUUID7_BUILD_TESTS=OFF \
    "$@"

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target uuid7_static uuid7_shared
