#!/usr/bin/env bash
set -euo pipefail

# Build integration-test executables against the uuid7 library artifacts.
#
# This script only builds. It does not run tests and it does not generate
# coverage reports. Use utils/run_ITs.sh for execution.
#
# Outputs:
#   build/ITs/libs/<profile>/libuuid7.a
#   build/ITs/libs/<profile>/libuuid7.so
#   build/ITs/<profile>/static/integration_test
#   build/ITs/<profile>/shared/integration_test
#   build/ITs/release_cov/static/integration_test
#   build/ITs/manifest.tsv

CC="${CC:-gcc}"

LIB_NAME="uuid7"
TEST_SOURCE="tests/ITs/integration_test.c"

START_DIR="$(pwd -P)"
cleanup() { cd -- "${START_DIR}" >/dev/null 2>&1 || true; }
trap cleanup EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROFILE_FILE="${SCRIPT_DIR}/gcc_build_profiles.sh"
BUILD_DIR="${ROOT_DIR}/build/ITs"
LIB_BUILD_DIR="${BUILD_DIR}/libs"
MANIFEST_FILE="${BUILD_DIR}/manifest.tsv"

cd -- "${ROOT_DIR}"

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

require_tool() {
    local tool="$1"

    if ! command -v "${tool}" >/dev/null 2>&1; then
        die "required tool not found: ${tool}"
    fi
}

require_array() {
    local array_name="$1"

    if ! declare -p "${array_name}" >/dev/null 2>&1; then
        die "required flag array is not defined: ${array_name}"
    fi
}

require_file() {
    local file_path="$1"

    [[ -f "${file_path}" ]] || die "required file not found: ${file_path}"
}

profile_is_known() {
    local requested_profile="$1"
    local known_profile

    for known_profile in "${GCC_BUILD_PROFILES[@]}"; do
        if [[ "${requested_profile}" == "${known_profile}" ]]; then
            return 0
        fi
    done

    return 1
}

add_profile_once() {
    local profile="$1"

    if [[ -z "${REQUESTED_PROFILE_SET[${profile}]:-}" ]]; then
        REQUESTED_PROFILES+=("${profile}")
        REQUESTED_PROFILE_SET["${profile}"]=1
    fi
}

add_library_build_once() {
    local build_name="$1"

    if [[ -z "${REQUESTED_LIBRARY_BUILD_SET[${build_name}]:-}" ]]; then
        REQUESTED_LIBRARY_BUILDS+=("${build_name}")
        REQUESTED_LIBRARY_BUILD_SET["${build_name}"]=1
    fi
}

write_manifest_header() {
    mkdir -p "${BUILD_DIR}"
    printf '# profile\tlinkage\texecutable\tlibrary\tcoverage\n' > "${MANIFEST_FILE}"
}

append_manifest_row() {
    local profile="$1"
    local linkage="$2"
    local executable="$3"
    local library="$4"
    local coverage="$5"

    printf '%s\t%s\t%s\t%s\t%s\n' \
        "${profile}" \
        "${linkage}" \
        "${executable}" \
        "${library}" \
        "${coverage}" >> "${MANIFEST_FILE}"
}

build_integration_test() {
    local output_profile="$1"
    local library_profile="$2"
    local linkage="$3"
    local cppflags_array_name="$4"
    local cflags_array_name="$5"
    local ldflags_array_name="$6"
    local coverage="$7"

    local -n profile_cppflags_ref="${cppflags_array_name}"
    local -n profile_cflags_ref="${cflags_array_name}"
    local -n profile_ldflags_ref="${ldflags_array_name}"

    local output_dir="${BUILD_DIR}/${output_profile}/${linkage}"
    local library_dir="${LIB_BUILD_DIR}/${library_profile}"
    local test_object="${output_dir}/integration_test.o"
    local executable="${output_dir}/integration_test"
    local library_path
    local -a cppflags=(
      "${profile_cppflags_ref[@]}"
      -DUUID7_TESTING
      -Iapp
      "${CMOCKA_CFLAGS[@]}"
    )
    local -a cflags=(
      "${profile_cflags_ref[@]}"
      -pthread
    )
    local -a ldflags=("${profile_ldflags_ref[@]}")
    local -a link_args=()

    mkdir -p "${output_dir}"

    case "${linkage}" in
        static)
            library_path="${library_dir}/lib${LIB_NAME}.a"
            require_file "${library_path}"
            link_args=("${library_path}")
            ;;
        shared)
            library_path="${library_dir}/lib${LIB_NAME}.so"
            require_file "${library_path}"
            link_args=(
              "${library_path}"
              "-Wl,-rpath,\$ORIGIN/../../libs/${library_profile}"
            )
            ;;
        *)
            die "unknown IT linkage: ${linkage}"
            ;;
    esac

    printf '  [%s/%s] compiling %s\n' "${output_profile}" "${linkage}" "${test_object}"
    "${CC}" \
        "${cppflags[@]}" \
        "${cflags[@]}" \
        -c "${TEST_SOURCE}" \
        -o "${test_object}"

    printf '  [%s/%s] linking %s\n' "${output_profile}" "${linkage}" "${executable}"
    "${CC}" \
        "${ldflags[@]}" \
        "${test_object}" \
        "${link_args[@]}" \
        -o "${executable}" \
        -pthread \
        "${CMOCKA_LIBS[@]}"

    append_manifest_row \
        "${output_profile}" \
        "${linkage}" \
        "${executable}" \
        "${library_path}" \
        "${coverage}"
}

build_profile_tests() {
    local profile="$1"
    local upper_profile="${profile^^}"
    local cppflags_array="CPPFLAGS_${upper_profile}"
    local cflags_array="CFLAGS_${upper_profile}"
    local ldflags_array="LDFLAGS_${upper_profile}"

    require_array "${cppflags_array}"
    require_array "${cflags_array}"
    require_array "${ldflags_array}"

    build_integration_test "${profile}" "${profile}" static \
        "${cppflags_array}" "${cflags_array}" "${ldflags_array}" 0
    build_integration_test "${profile}" "${profile}" shared \
        "${cppflags_array}" "${cflags_array}" "${ldflags_array}" 0
}

build_release_coverage_test() {
    local -a cppflags_release_cov_it=("${CPPFLAGS_RELEASE[@]}")
    local -a cflags_release_cov_it=("${CFLAGS_RELEASE[@]}")
    local -a ldflags_release_cov_it=(
      "${LDFLAGS_RELEASE[@]}"
      "${LDFLAGS_INSTRUMENT_COVERAGE[@]}"
    )

    # One coverage executable is intentional: running both static and shared
    # coverage variants would double-count the same library code in reports.
    build_integration_test release_cov release_cov static \
        cppflags_release_cov_it \
        cflags_release_cov_it \
        ldflags_release_cov_it \
        1
}

require_tool "${CC}"
require_tool pkg-config
require_file "${PROFILE_FILE}"
require_file "${TEST_SOURCE}"
require_file "${SCRIPT_DIR}/build_libs.sh"

source "${PROFILE_FILE}"

require_array GCC_BUILD_PROFILES
require_array CPPFLAGS_RELEASE
require_array CFLAGS_RELEASE
require_array LDFLAGS_RELEASE
require_array LDFLAGS_INSTRUMENT_COVERAGE

if ! pkg-config --exists cmocka; then
    die "cmocka not found (pkg-config --exists cmocka failed)"
fi

read -r -a CMOCKA_CFLAGS <<< "$(pkg-config --cflags cmocka)"
read -r -a CMOCKA_LIBS <<< "$(pkg-config --libs cmocka)"

declare -a REQUESTED_PROFILES=()
declare -A REQUESTED_PROFILE_SET=()
declare -a REQUESTED_LIBRARY_BUILDS=()
declare -A REQUESTED_LIBRARY_BUILD_SET=()
BUILD_COVERAGE=0

if (($# == 0)); then
    for profile in "${GCC_BUILD_PROFILES[@]}"; do
        add_profile_once "${profile}"
    done
    BUILD_COVERAGE=1
else
    for requested_build in "$@"; do
        case "${requested_build}" in
            all|profiles)
                for profile in "${GCC_BUILD_PROFILES[@]}"; do
                    add_profile_once "${profile}"
                done
                ;;
            coverage|release_cov)
                BUILD_COVERAGE=1
                ;;
            *)
                if profile_is_known "${requested_build}"; then
                    add_profile_once "${requested_build}"
                else
                    die "unknown IT build profile: ${requested_build}"
                fi
                ;;
        esac
    done
fi

for profile in "${REQUESTED_PROFILES[@]}"; do
    add_library_build_once "${profile}"
done

if ((BUILD_COVERAGE)); then
    add_library_build_once release_cov
fi

if ((${#REQUESTED_LIBRARY_BUILDS[@]} == 0)); then
    die "no IT builds requested"
fi

printf 'building test-hook-enabled %s libraries in %s\n' "${LIB_NAME}" "${LIB_BUILD_DIR}"
UUID7_ENABLE_TEST_HOOKS=1 \
UUID7_BUILD_DIR="${LIB_BUILD_DIR}" \
    "${SCRIPT_DIR}/build_libs.sh" "${REQUESTED_LIBRARY_BUILDS[@]}"

printf '\nbuilding integration tests in %s\n' "${BUILD_DIR}"
printf 'compiler: %s\n' "${CC}"
printf 'manifest: %s\n' "${MANIFEST_FILE}"

write_manifest_header

for profile in "${REQUESTED_PROFILES[@]}"; do
    build_profile_tests "${profile}"
done

if ((BUILD_COVERAGE)); then
    build_release_coverage_test
fi

printf '\nbuilt integration-test executables:\n'
while IFS=$'\t' read -r profile linkage executable library coverage; do
    [[ "${profile}" == '# profile' ]] && continue
    printf '  %s/%s -> %s\n' "${profile}" "${linkage}" "${executable}"
    if [[ "${coverage}" == "1" ]]; then
        printf '    coverage library: %s\n' "${library}"
    fi
done < "${MANIFEST_FILE}"
