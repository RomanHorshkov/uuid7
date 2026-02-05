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
mkdir -p "$BUILD_DIR"
mkdir -p "${ROOT_DIR}/tests/results/ITs"

CFLAGS=(
  -std=c11
  -O2
  -g
  -D_GNU_SOURCE
  -DUUID7_TESTING
  -Iapp
)

if ! pkg-config --exists cmocka; then
  echo "cmocka not found (pkg-config --exists cmocka failed)"
  exit 1
fi

read -r -a CMOCKA_CFLAGS <<< "$(pkg-config --cflags cmocka)"
read -r -a CMOCKA_LIBS <<< "$(pkg-config --libs cmocka)"

gcc "${CFLAGS[@]}" -c app/uuid7.c -o "${BUILD_DIR}/uuid7.o"
gcc "${CFLAGS[@]}" "${CMOCKA_CFLAGS[@]}" -c tests/ITs/integration_test.c -o "${BUILD_DIR}/integration_test.o"
gcc -O2 -g "${BUILD_DIR}/uuid7.o" "${BUILD_DIR}/integration_test.o" -o "${BUILD_DIR}/integration_test" -pthread "${CMOCKA_LIBS[@]}"

"${BUILD_DIR}/integration_test"
