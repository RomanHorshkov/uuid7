#!/usr/bin/env bash
set -euo pipefail

# Run integration-test executables produced by utils/build_ITs.sh.
#
# This script does not build. It consumes build/ITs/manifest.tsv and writes
# per-binary result logs. Coverage is generated only from the release-derived
# coverage executable marked in the manifest.

START_DIR="$(pwd -P)"
cleanup() { cd -- "${START_DIR}" >/dev/null 2>&1 || true; }
trap cleanup EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/ITs"
MANIFEST_FILE="${BUILD_DIR}/manifest.tsv"
DEFAULT_RESULT_DIR="${ROOT_DIR}/tests/results/ITs"
RUN_RESULT_DIR="${UUID7_RESULTS_RUN_DIR:-${DEFAULT_RESULT_DIR}}"
COVERAGE_OBJECT_DIR="${BUILD_DIR}/libs/release_cov"

if [[ -n "${UUID7_RESULTS_RUN_DIR:-}" ]]; then
    RESULT_DIR="${RUN_RESULT_DIR}/ITs"
    SUMMARY_FILE="${RUN_RESULT_DIR}/ITs_summary.tsv"
    COVERAGE_RESULT_DIR="${RUN_RESULT_DIR}/coverage/release"
else
    RESULT_DIR="${DEFAULT_RESULT_DIR}"
    SUMMARY_FILE="${DEFAULT_RESULT_DIR}/ITs_summary.tsv"
    COVERAGE_RESULT_DIR="${DEFAULT_RESULT_DIR}/coverage/release"
fi

cd -- "${ROOT_DIR}"

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

require_file() {
    local file_path="$1"

    [[ -f "${file_path}" ]] || die "required file not found: ${file_path}"
}

write_summary_header() {
    mkdir -p "${RUN_RESULT_DIR}"
    printf '# status\tprofile\tlinkage\tresult\texecutable\tlibrary\tcoverage\n' > "${SUMMARY_FILE}"
}

append_summary_row() {
    local status="$1"
    local profile="$2"
    local linkage="$3"
    local result_file="$4"
    local executable="$5"
    local library="$6"
    local coverage="$7"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${status}" \
        "${profile}" \
        "${linkage}" \
        "${result_file}" \
        "${executable}" \
        "${library}" \
        "${coverage}" >> "${SUMMARY_FILE}"
}

run_one_test() {
    local profile="$1"
    local linkage="$2"
    local executable="$3"
    local library="$4"
    local result_file="$5"
    local rc=0
    local library_dir
    local previous_ld_path

    printf '\n[%s/%s]\n' "${profile}" "${linkage}"
    printf '  executable: %s\n' "${executable}"
    printf '  library:    %s\n' "${library}"
    printf '  result:     %s\n' "${result_file}"

    mkdir -p "${result_file%/*}"

    if [[ "${linkage}" == "shared" ]]; then
        library_dir="$(cd "$(dirname "${library}")" && pwd)"
        previous_ld_path="${LD_LIBRARY_PATH:-}"

        if [[ -n "${previous_ld_path}" ]]; then
            LD_LIBRARY_PATH="${library_dir}:${previous_ld_path}" \
                "${executable}" > "${result_file}" 2>&1 || rc=$?
        else
            LD_LIBRARY_PATH="${library_dir}" \
                "${executable}" > "${result_file}" 2>&1 || rc=$?
        fi
    else
        "${executable}" > "${result_file}" 2>&1 || rc=$?
    fi

    cat "${result_file}"

    if ((rc != 0)); then
        printf '  status: FAIL exit=%d\n' "${rc}"
    else
        printf '  status: PASS\n'
    fi

    return "${rc}"
}

generate_release_coverage() {
    if ! command -v gcovr >/dev/null 2>&1; then
        printf '[coverage] gcovr not found; install it to generate reports\n' >&2
        return 1
    fi

    mkdir -p "${COVERAGE_RESULT_DIR}"

    printf '\n[coverage]\n'
    printf '  profile:          release_cov/static\n'
    printf '  object directory: %s\n' "${COVERAGE_OBJECT_DIR}"
    printf '  output directory: %s\n' "${COVERAGE_RESULT_DIR}"

    gcovr -r "${ROOT_DIR}" \
        --object-directory "${COVERAGE_OBJECT_DIR}" \
        --exclude 'tests/' \
        --gcov-ignore-parse-errors negative_hits.warn_once_per_file \
        --html --html-details \
        -o "${COVERAGE_RESULT_DIR}/ITs_release_coverage.html"

    gcovr -r "${ROOT_DIR}" \
        --object-directory "${COVERAGE_OBJECT_DIR}" \
        --exclude 'tests/' \
        --gcov-ignore-parse-errors negative_hits.warn_once_per_file \
        --xml \
        -o "${COVERAGE_RESULT_DIR}/ITs_release_coverage.xml"

    gcovr -r "${ROOT_DIR}" \
        --object-directory "${COVERAGE_OBJECT_DIR}" \
        --exclude 'tests/' \
        --gcov-ignore-parse-errors negative_hits.warn_once_per_file \
        --json-summary \
        -o "${COVERAGE_RESULT_DIR}/coverage-summary.json"

    printf '[coverage] report ready: %s\n' \
        "${COVERAGE_RESULT_DIR}/ITs_release_coverage.html"
}

print_summary() {
    local status profile linkage result_file executable library coverage

    printf '\n[IT summary]\n'
    printf '  summary: %s\n' "${SUMMARY_FILE}"

    while IFS=$'\t' read -r status profile linkage result_file executable library coverage; do
        [[ -z "${status}" ]] && continue
        [[ "${status}" == \#* ]] && continue
        printf '  %-4s  %-11s %-7s  %s\n' \
            "${status}" \
            "${profile}" \
            "${linkage}" \
            "${result_file}"
    done < "${SUMMARY_FILE}"
}

require_file "${MANIFEST_FILE}"
mkdir -p "${RESULT_DIR}"
write_summary_header

TEST_RC=0
COVERAGE_RAN=0
COVERAGE_RESET=0

printf '[run_ITs]\n'
printf '  manifest: %s\n' "${MANIFEST_FILE}"
printf '  result root: %s\n' "${RUN_RESULT_DIR}"
printf '  summary: %s\n' "${SUMMARY_FILE}"

while IFS=$'\t' read -r profile linkage executable library coverage; do
    [[ -z "${profile}" ]] && continue
    [[ "${profile}" == \#* ]] && continue

    require_file "${executable}"
    [[ -x "${executable}" ]] || die "test executable is not executable: ${executable}"
    require_file "${library}"

    if [[ "${coverage}" == "1" && "${COVERAGE_RESET}" == "0" ]]; then
        find "${COVERAGE_OBJECT_DIR}" -name '*.gcda' -delete
        COVERAGE_RESET=1
    fi

    result_file="${RESULT_DIR}/${profile}/${linkage}/integration_result.txt"

    if run_one_test "${profile}" "${linkage}" "${executable}" "${library}" "${result_file}"; then
        append_summary_row PASS "${profile}" "${linkage}" "${result_file}" \
            "${executable}" "${library}" "${coverage}"
    else
        TEST_RC=1
        append_summary_row FAIL "${profile}" "${linkage}" "${result_file}" \
            "${executable}" "${library}" "${coverage}"
    fi

    if [[ "${coverage}" == "1" ]]; then
        COVERAGE_RAN=1
    fi
done < "${MANIFEST_FILE}"

if ((COVERAGE_RAN)); then
    generate_release_coverage || TEST_RC=1
fi

print_summary
exit "${TEST_RC}"
