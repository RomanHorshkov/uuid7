#!/usr/bin/env bash
set -euo pipefail

if ! command -v gcovr >/dev/null 2>&1; then
    echo "gcovr is required for coverage generation. Please install it and retry." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/coverage}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
COVERAGE_DIR="${COVERAGE_DIR:-${ROOT_DIR}/coverage}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DUUID7_BUILD_TESTS=ON \
    -DUUID7_ENABLE_COVERAGE=ON \
    "$@"

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target uuid7_tests
ctest --test-dir "${BUILD_DIR}" --output-on-failure --build-config "${BUILD_TYPE}"

mkdir -p "${COVERAGE_DIR}"

COMMON_GCOVR_ARGS=(
    --root "${ROOT_DIR}"
    --object-directory "${BUILD_DIR}"
    --exclude "${ROOT_DIR}/tests"
    --gcov-ignore-parse-errors
)

gcovr "${COMMON_GCOVR_ARGS[@]}" --html --html-details \
    -o "${COVERAGE_DIR}/uuid7-coverage.html"

gcovr "${COMMON_GCOVR_ARGS[@]}" --xml \
    -o "${COVERAGE_DIR}/uuid7-coverage.xml"

gcovr "${COMMON_GCOVR_ARGS[@]}" --txt --txt-summary --txt-metric line

echo "Coverage reports written to ${COVERAGE_DIR}"
