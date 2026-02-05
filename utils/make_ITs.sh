#!/usr/bin/env bash
set -euo pipefail

# Build + run the integration test (Release-ish: -O2, with symbols: -g).
#
# Outputs:
#   build/ITs/integration_test
#   tests/results/ITs/integration_result.txt

START_DIR="$(pwd -P)"
cleanup() { cd -- "$START_DIR"; }
trap cleanup EXIT

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd -- "$ROOT_DIR"

BUILD_DIR="${ROOT_DIR}/build/ITs"
RESULT_DIR="${ROOT_DIR}/tests/results/ITs"
mkdir -p "$BUILD_DIR"
mkdir -p "${RESULT_DIR}"

CFLAGS=(
  -std=c11
  -D_GNU_SOURCE
  -DUUID7_TESTING
  -Iapp
  -O0
  -g
  --coverage
)

if ! pkg-config --exists cmocka; then
  echo "cmocka not found (pkg-config --exists cmocka failed)"
  exit 1
fi

read -r -a CMOCKA_CFLAGS <<< "$(pkg-config --cflags cmocka)"
read -r -a CMOCKA_LIBS <<< "$(pkg-config --libs cmocka)"

gcc "${CFLAGS[@]}" -c app/uuid7.c -o "${BUILD_DIR}/uuid7.o"
gcc "${CFLAGS[@]}" "${CMOCKA_CFLAGS[@]}" -c tests/ITs/integration_test.c -o "${BUILD_DIR}/integration_test.o"
gcc --coverage -O0 -g "${BUILD_DIR}/uuid7.o" "${BUILD_DIR}/integration_test.o" -o "${BUILD_DIR}/integration_test" -pthread "${CMOCKA_LIBS[@]}"

"${BUILD_DIR}/integration_test"

if ! command -v gcovr >/dev/null 2>&1; then
  echo "[coverage] gcovr not found; install it to generate reports"
  exit 1
fi

printf '[coverage] generating combined reports via gcovr...\n'
gcovr -r "${ROOT_DIR}" \
  --object-directory "${BUILD_DIR}" \
  --exclude 'tests/' \
  --gcov-ignore-parse-errors negative_hits.warn_once_per_file \
  --html --html-details \
  -o "${RESULT_DIR}/ITs_all_coverage.html"

gcovr -r "${ROOT_DIR}" \
  --object-directory "${BUILD_DIR}" \
  --exclude 'tests/' \
  --gcov-ignore-parse-errors negative_hits.warn_once_per_file \
  --xml \
  -o "${RESULT_DIR}/ITs_all_coverage.xml"

gcovr -r "${ROOT_DIR}" \
  --object-directory "${BUILD_DIR}" \
  --exclude 'tests/' \
  --gcov-ignore-parse-errors negative_hits.warn_once_per_file \
  --json-summary \
  -o "${RESULT_DIR}/coverage-summary.json"

printf '[coverage] report ready: %s\n' "${RESULT_DIR}/ITs_all_coverage.html"
