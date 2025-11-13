#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/tests}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

if [[ ! -d "${BUILD_DIR}" ]]; then
    "${SCRIPT_DIR}/build_tests.sh"
fi

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target uuid7_tests
ctest --test-dir "${BUILD_DIR}" --output-on-failure --build-config "${BUILD_TYPE}"
