#!/usr/bin/env bash
set -euo pipefail

START_DIR="$(pwd -P)"
cleanup() { cd -- "$START_DIR"; }
trap cleanup EXIT

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd -- "$ROOT_DIR"

BUILD_DIR="${ROOT_DIR}/build/stress"
RESULT_DIR="${ROOT_DIR}/tests/results/stress"

mkdir -p "${BUILD_DIR}"
mkdir -p "${RESULT_DIR}"

./utils/make_libs.sh

WARN_FLAGS=(
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wformat=2
  -Wconversion
  -Wnull-dereference
  -Wdouble-promotion
  -Wduplicated-cond
  -Wduplicated-branches
  -Wlogical-op
  -Wfloat-equal
)

CFLAGS=(
  -std=c11
  -O3
  -g
  -pthread
  -Iapp
  -Itests/stress
  "${WARN_FLAGS[@]}"
)

LIBS=(
  build/libuuid7.a
  -lm
  -pthread
)

gcc "${CFLAGS[@]}" tests/stress/stress.c -o "${BUILD_DIR}/stress" "${LIBS[@]}"
gcc "${CFLAGS[@]}" tests/stress/stress_mt.c -o "${BUILD_DIR}/stress_mt" "${LIBS[@]}"

"${BUILD_DIR}/stress" | tee "${RESULT_DIR}/stress_result.txt"
"${BUILD_DIR}/stress_mt" | tee "${RESULT_DIR}/stress_mt_result.txt"

printf 'stress results written to:\n'
printf '  %s\n' "${RESULT_DIR}/stress_result.txt"
printf '  %s\n' "${RESULT_DIR}/stress_mt_result.txt"
