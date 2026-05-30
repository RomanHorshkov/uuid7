#!/usr/bin/env bash
# =============================================================================
# uuid7 library builder
# =============================================================================
#
# This script builds the uuid7 library artifacts from app/uuid7.c.
#
# It deliberately does one job only:
#   - compile app/uuid7.c with the selected GCC profile flags;
#   - produce a static archive:  build/<profile>/libuuid7.a;
#   - produce a shared library:  build/<profile>/libuuid7.so.<VERSION>;
#   - produce shared-library symlinks:
#       build/<profile>/libuuid7.so.<MAJOR>
#       build/<profile>/libuuid7.so
#   - optionally produce a coverage-instrumented release variant:
#       build/release_cov/libuuid7.a
#       build/release_cov/libuuid7.so.<VERSION>
#
# It does NOT build tests.
# It does NOT run tests.
# It does NOT call the other utils/*.sh scripts.
#
# The source of truth for build policy is:
#   utils/gcc_build_profiles.sh
#
# That profile file owns the actual flag arrays:
#   CPPFLAGS_RELEASE, CFLAGS_RELEASE, LDFLAGS_RELEASE
#   CPPFLAGS_DEBUG,   CFLAGS_DEBUG,   LDFLAGS_DEBUG
#   CFLAGS_SHARED,    LDFLAGS_SHARED
#   CFLAGS_INSTRUMENT_COVERAGE, LDFLAGS_INSTRUMENT_COVERAGE
# and so on.
#
# This file is the builder. The profile file is the policy.
# =============================================================================

set -euo pipefail

# Tools can be overridden by the caller if needed:
#   CC=clang ./utils/build_libs.sh release
#   AR=gcc-ar RANLIB=gcc-ranlib ./utils/build_libs.sh native
CC="${CC:-gcc}"
AR="${AR:-ar}"
RANLIB="${RANLIB:-ranlib}"

LIB_NAME="uuid7"
SOURCE_FILE="app/uuid7.c"
VERSION_FILE="VERSION"
ENABLE_TEST_HOOKS="${UUID7_ENABLE_TEST_HOOKS:-0}"

# Internal test scripts can override this to keep test-hook-enabled libraries
# separate from production libraries.
UUID7_BUILD_DIR="${UUID7_BUILD_DIR:-}"

# Project-local static archives to merge into libuuid7.a.
#
# A .a file is not a fully linked executable. It is an archive of object files.
# That means it can contain:
#   - uuid7.o from this project;
#   - object files extracted from other project-local .a archives.
#
# It should NOT try to stuff libc, libasan, libtsan, or random system shared
# libraries into itself. Those runtime libraries are resolved later by the final
# executable or shared-library link step.
#
# uuid7 currently has no project-local static library dependencies, so this is
# empty. If you later add a local archive that must be embedded inside every
# libuuid7.a, add its path here, for example:
#   STATIC_ARCHIVE_DEPS=("third_party/something/libsomething.a")
STATIC_ARCHIVE_DEPS=()

# Libraries or linker arguments needed only when linking libuuid7.so.
#
# uuid7 currently does not need extra libraries. If that changes, add the link
# arguments here, for example:
#   SHARED_LINK_LIBS=(-lm)
SHARED_LINK_LIBS=()

EXTRA_CPPFLAGS=()
if [[ "${ENABLE_TEST_HOOKS}" == "1" ]]; then
    EXTRA_CPPFLAGS=(-DUUID7_TESTING)
fi

# Keep track of what this script actually produced so the final artifact list is
# truthful instead of merely listing what we hoped would exist.
BUILT_ARTIFACTS=()

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

print_artifact_report() {
    local artifact_path="$1"

    printf '    artifact: %s\n' "${artifact_path}"
    printf '      size:   %s bytes\n' "$(wc -c < "${artifact_path}")"

    if command -v file >/dev/null 2>&1; then
        printf '      type:   %s\n' "$(file -b "${artifact_path}")"
    else
        printf '      type:   file(1) not available\n'
    fi
}

# Remove every flag from input_flags that also appears in blocked_flags.
#
# Profiles may contain executable-only flags, for example:
#   CFLAGS_EXE_HARDENING=(-fPIE)
#   LDFLAGS_EXE_HARDENING=(-pie -Wl,-z,relro -Wl,-z,now)
#
# Those are correct for executable builds, but this script builds libraries.
# Shared libraries need:
#   CFLAGS_SHARED=(-fPIC)
#   LDFLAGS_SHARED=(-shared)
filter_flags() {
    local -n input_flags="$1"
    local -n blocked_flags="$2"
    local -n output_flags="$3"
    local flag
    local blocked_flag
    local blocked

    output_flags=()

    for flag in "${input_flags[@]}"; do
        blocked=0

        for blocked_flag in "${blocked_flags[@]}"; do
            if [[ "${flag}" == "${blocked_flag}" ]]; then
                blocked=1
                break
            fi
        done

        if ((blocked == 0)); then
            output_flags+=("${flag}")
        fi
    done

    return 0
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

read_project_version() {
    [[ -e "${VERSION_FILE}" ]] || die "VERSION file does not exist: ${ROOT_DIR}/${VERSION_FILE}"
    [[ -f "${VERSION_FILE}" ]] || die "VERSION path is not a file: ${ROOT_DIR}/${VERSION_FILE}"

    VERSION="$(< "${VERSION_FILE}")"
    [[ -n "${VERSION}" ]] || die "VERSION file is empty: ${ROOT_DIR}/${VERSION_FILE}"

    IFS='.' read -r MAJOR MINOR PATCH_EXTRA <<< "${VERSION}"
    [[ -n "${MAJOR:-}" && -n "${MINOR:-}" && -n "${PATCH_EXTRA:-}" ]] \
        || die "VERSION must look like MAJOR.MINOR.PATCH, got: ${VERSION}"
}

create_static_archive() {
    local output_archive="$1"
    local own_object="$2"
    local archive_dir="${output_archive%/*}"
    local merge_root
    local dep_archive
    local dep_archive_path
    local dep_index=0
    local dep_dir
    local member
    local -a archive_members=("${own_object}")

    rm -f "${output_archive}"

    if ((${#STATIC_ARCHIVE_DEPS[@]} > 0)); then
        merge_root="$(mktemp -d "${archive_dir}/archive-merge.XXXXXX")"

        for dep_archive in "${STATIC_ARCHIVE_DEPS[@]}"; do
            if [[ "${dep_archive}" = /* ]]; then
                dep_archive_path="${dep_archive}"
            else
                dep_archive_path="${ROOT_DIR}/${dep_archive}"
            fi

            [[ -f "${dep_archive_path}" ]] || die "static dependency archive not found: ${dep_archive}"

            dep_index=$((dep_index  1))
            dep_dir="${merge_root}/dep_${dep_index}"
            mkdir -p "${dep_dir}"

            # ar extracts archive members into the current directory, so isolate
            # each dependency in its own directory to avoid filename collisions.
            (cd "${dep_dir}" && "${AR}" x "${dep_archive_path}")

            while IFS= read -r -d '' member; do
                archive_members=("${member}")
            done < <(find "${dep_dir}" -type f -print0 | sort -z)
        done

        "${AR}" rcs "${output_archive}" "${archive_members[@]}"
        rm -rf "${merge_root}"
    else
        "${AR}" rcs "${output_archive}" "${own_object}"
    fi

    if command -v "${RANLIB}" >/dev/null 2>&1; then
        "${RANLIB}" "${output_archive}"
    fi
}

create_release_root_aliases() {
    local label="$1"
    local static_library="$2"
    local shared_library="$3"
    local major_shared_link="$4"
    local unversioned_shared_link="$5"
    local root_static_library="${BUILD_DIR}/lib${LIB_NAME}.a"
    local root_shared_library="${BUILD_DIR}/lib${LIB_NAME}.so.${VERSION}"
    local root_major_shared_link="${BUILD_DIR}/lib${LIB_NAME}.so.${MAJOR}"
    local root_unversioned_shared_link="${BUILD_DIR}/lib${LIB_NAME}.so"

    [[ "${label}" == "release" ]] || return 0
    [[ "${BUILD_DIR}" == "${ROOT_DIR}/build" ]] || return 0
    [[ "${ENABLE_TEST_HOOKS}" != "1" ]] || return 0

    printf '  linking release compatibility aliases in: %s\n' "${BUILD_DIR}"
    ln -sfn "${static_library#"${BUILD_DIR}/"}" "${root_static_library}"
    ln -sfn "${shared_library#"${BUILD_DIR}/"}" "${root_shared_library}"
    ln -sfn "${major_shared_link#"${BUILD_DIR}/"}" "${root_major_shared_link}"
    ln -sfn "${unversioned_shared_link#"${BUILD_DIR}/"}" "${root_unversioned_shared_link}"

    BUILT_ARTIFACTS=(
      "${root_shared_library}"
      "${root_major_shared_link}"
      "${root_unversioned_shared_link}"
      "${root_static_library}"
    )
}

build_library_variant() {
    local label="$1"
    local output_dir="$2"
    local cppflags_array_name="$3"
    local cflags_array_name="$4"
    local ldflags_array_name="$5"

    local -n raw_cppflags_ref="${cppflags_array_name}"
    local -n raw_cflags_ref="${cflags_array_name}"
    local -n raw_ldflags_ref="${ldflags_array_name}"

    local -a cppflags=("${raw_cppflags_ref[@]}" "${EXTRA_CPPFLAGS[@]}")
    local -a raw_cflags=("${raw_cflags_ref[@]}")
    local -a raw_ldflags=("${raw_ldflags_ref[@]}")
    local -a library_cflags=()
    local -a library_ldflags=()

    local static_object="${output_dir}/${LIB_NAME}.o"
    local shared_object="${output_dir}/${LIB_NAME}.pic.o"
    local static_library="${output_dir}/lib${LIB_NAME}.a"
    local shared_soname="lib${LIB_NAME}.so.${MAJOR}"
    local shared_library="${output_dir}/lib${LIB_NAME}.so.${VERSION}"
    local major_shared_link="${output_dir}/${shared_soname}"
    local unversioned_shared_link="${output_dir}/lib${LIB_NAME}.so"

    filter_flags raw_cflags CFLAGS_EXE_HARDENING library_cflags
    filter_flags raw_ldflags LDFLAGS_EXE_HARDENING library_ldflags

    mkdir -p "${output_dir}"

    printf '\n[%s]\n' "${label}"

    printf '  compiling shared-library object: %s\n' "${shared_object}"
    "${CC}" \
        "${cppflags[@]}" \
        "${library_cflags[@]}" \
        "${CFLAGS_SHARED[@]}" \
        -c "${SOURCE_FILE}" \
        -o "${shared_object}"

    printf '  compiling static-library object: %s\n' "${static_object}"
    "${CC}" \
        "${cppflags[@]}" \
        "${library_cflags[@]}" \
        -c "${SOURCE_FILE}" \
        -o "${static_object}"

    printf '  linking shared library:          %s\n' "${shared_library}"
    "${CC}" \
        "${LDFLAGS_SHARED[@]}" \
        "${library_ldflags[@]}" \
        -Wl,-soname,"${shared_soname}" \
        -o "${shared_library}" \
        "${shared_object}" \
        "${SHARED_LINK_LIBS[@]}"

    printf '  linking shared-library aliases:  %s, %s\n' \
        "${major_shared_link}" \
        "${unversioned_shared_link}"
    ln -sfn "${shared_library##*/}" "${major_shared_link}"
    ln -sfn "${major_shared_link##*/}" "${unversioned_shared_link}"

    printf '  creating static library:         %s\n' "${static_library}"
    create_static_archive "${static_library}" "${static_object}"

    create_release_root_aliases \
        "${label}" \
        "${static_library}" \
        "${shared_library}" \
        "${major_shared_link}" \
        "${unversioned_shared_link}"

    printf '  output summary:\n'
    print_artifact_report "${shared_library}"
    print_artifact_report "${static_library}"

    BUILT_ARTIFACTS=(
      "${shared_library}"
      "${major_shared_link}"
      "${unversioned_shared_link}"
      "${static_library}"
    )
}

build_normal_profile() {
    local profile="$1"
    local upper_profile="${profile^^}"
    local cppflags_array="CPPFLAGS_${upper_profile}"
    local cflags_array="CFLAGS_${upper_profile}"
    local ldflags_array="LDFLAGS_${upper_profile}"
    local profile_build_dir="${BUILD_DIR}/${profile}"

    require_array "${cppflags_array}"
    require_array "${cflags_array}"
    require_array "${ldflags_array}"

    build_library_variant \
        "${profile}" \
        "${profile_build_dir}" \
        "${cppflags_array}" \
        "${cflags_array}" \
        "${ldflags_array}"
}

build_coverage_profile() {
    local coverage_profile="release_cov"
    local coverage_build_dir="${BUILD_DIR}/${coverage_profile}"

    if ! command -v gcov >/dev/null 2>&1; then
        printf '\n[%s]\n' "${coverage_profile}"
        printf '  skipped: gcov not found\n'
        return 0
    fi

    local -a coverage_cppflags=("${CPPFLAGS_RELEASE[@]}")
    local -a coverage_cflags=(
      "${CFLAGS_RELEASE[@]}"
      "${CFLAGS_INSTRUMENT_COVERAGE[@]}"
    )
    local -a coverage_ldflags=(
      "${LDFLAGS_RELEASE[@]}"
      "${LDFLAGS_INSTRUMENT_COVERAGE[@]}"
    )

    printf '\n[%s setup]\n' "${coverage_profile}"
    printf '  base profile:          release\n'
    printf '  extra instrumentation: --coverage\n'

    build_library_variant \
        "${coverage_profile}" \
        "${coverage_build_dir}" \
        coverage_cppflags \
        coverage_cflags \
        coverage_ldflags
}

START_DIR="$(pwd -P)"
cleanup() {
    cd -- "${START_DIR}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROFILE_FILE="${SCRIPT_DIR}/gcc_build_profiles.sh"
BUILD_DIR="${UUID7_BUILD_DIR:-${ROOT_DIR}/build}"

cd -- "${ROOT_DIR}"

[[ -f "${PROFILE_FILE}" ]] || die "gcc profile file not found: ${PROFILE_FILE}"
[[ -f "${SOURCE_FILE}" ]] || die "source file not found: ${ROOT_DIR}/${SOURCE_FILE}"

require_tool "${CC}"
require_tool "${AR}"

read_project_version

# The sourced file defines all build flag arrays used by the functions above.
source "${PROFILE_FILE}"

require_array GCC_BUILD_PROFILES
require_array CFLAGS_SHARED
require_array LDFLAGS_SHARED
require_array CFLAGS_EXE_HARDENING
require_array LDFLAGS_EXE_HARDENING
require_array CPPFLAGS_RELEASE
require_array CFLAGS_RELEASE
require_array LDFLAGS_RELEASE
require_array CFLAGS_INSTRUMENT_COVERAGE
require_array LDFLAGS_INSTRUMENT_COVERAGE

mkdir -p "${BUILD_DIR}"

printf 'building %s libraries in %s\n' "${LIB_NAME}" "${BUILD_DIR}"
printf 'version: %s (soname major: %s)\n' "${VERSION}" "${MAJOR}"
printf 'using profile policy from %s\n' "${PROFILE_FILE}"
printf 'compiler: %s\n' "${CC}"
printf 'archiver: %s\n' "${AR}"
if [[ "${ENABLE_TEST_HOOKS}" == "1" ]]; then
    printf 'test hooks: enabled (-DUUID7_TESTING)\n'
fi

if (($# == 0)); then
    REQUESTED_BUILDS=("${GCC_BUILD_PROFILES[@]}" release_cov)
else
    REQUESTED_BUILDS=("$@")
fi

for requested_build in "${REQUESTED_BUILDS[@]}"; do
    case "${requested_build}" in
        all|profiles)
            for profile in "${GCC_BUILD_PROFILES[@]}"; do
                build_normal_profile "${profile}"
            done
            build_coverage_profile
            ;;
        coverage|release_cov)
            build_coverage_profile
            ;;
        *)
            if profile_is_known "${requested_build}"; then
                build_normal_profile "${requested_build}"
            else
                die "unknown build profile: ${requested_build}"
            fi
            ;;
    esac
done

printf '\nbuilt artifacts:\n'
if ((${#BUILT_ARTIFACTS[@]} == 0)); then
    printf '  none\n'
else
    for artifact in "${BUILT_ARTIFACTS[@]}"; do
        printf '  %s\n' "${artifact}"
    done
fi
