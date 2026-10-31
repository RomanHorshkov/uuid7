#!/usr/bin/env bash
# shellcheck shell=bash
# uuid7_project.sh — uuid7's declarations for the pinned kyr-build (every other script in utils/ is its wrapper): the library,
# the suites, the debs and what the ABI gate treats as public. Declarations only.
KYR_LIB="uuid7"
KYR_LIB_SOURCES=(app/uuid7.c)
KYR_LIB_HEADER="app/uuid7.h"
KYR_LIB_MAP="utils/uuid7.map"
KYR_LIB_CPPFLAGS=(-I"${ROOT_DIR}/app")
KYR_LIB_LDLIBS=()
# The deterministic-clock and RNG-fault hooks (app/uuid7_test.h): the integration run's libraries only, never a shipped one.
KYR_TEST_HOOKS_CPPFLAGS=(-DUUID7_TESTING)
KYR_TEST_HOOKS_RUNS=(it)

# The integration suites against every profile's archive and shared object, then the coverage run. faults defines getrandom, open,
# read and clock_gettime itself (forwarding to libc unless a test scripts a failure): the library's calls resolve to them in every cell.
KYR_IT_SUITES=(it faults)
KYR_SUITE_IT_SOURCES=(tests/ITs/integration_test.c)
KYR_SUITE_IT_CPPFLAGS=(-pthread)
KYR_SUITE_IT_LDLIBS=(-lcmocka -pthread)
KYR_SUITE_FAULTS_SOURCES=(tests/ITs/faults_test.c)
KYR_SUITE_FAULTS_LDLIBS=(-lcmocka)
KYR_IT_PROFILES=("${GCC_BUILD_PROFILES[@]}")
KYR_IT_LINKAGES=(static shared)
KYR_IT_COVERAGE=1
# The coverage run's floor (the plan's B7): 99.3 % lines, 94.7 % branches measured on 2026-10-31, less the one line and branch only a
# race reaches (uuid7_gen's CAS retry, taken when the threaded test's producers collide), rounded down.
KYR_IT_FAIL_UNDER_LINE=98
KYR_IT_FAIL_UNDER_BRANCH=93
# The coverage badge (.github/badges/coverage.svg, the README's Coverage line, and its endpoint JSON) from the gated run's summary.
KYR_PIPELINE_EXTRA=("post|coverage-badge|[[ ! -f tests/results/ITs/coverage/coverage-summary.json ]] || python3 build/sysroot/share/kyr-build/coverage_badge.py tests/results/ITs/coverage/coverage-summary.json .github/badges/coverage.json .github/badges/coverage.svg")
# Stress: the single-threaded and the multithreaded benchmark, every profile, static and shared.
KYR_STRESS_SUITES=(stress)
KYR_SUITE_STRESS_SOURCES=(tests/stress/stress.c tests/stress/stress_mt.c)
KYR_SUITE_STRESS_CPPFLAGS=(-pthread -I"${ROOT_DIR}/tests/stress")
KYR_SUITE_STRESS_LDLIBS=(-lm -pthread)
KYR_STRESS_PROFILES=("${GCC_BUILD_PROFILES[@]}")
KYR_STRESS_LINKAGES=(static shared)
KYR_STRESS_METRICS=("stress|cost per uuid|throughput" "stress_mt|all per-thread cost samples|aggregate throughput per run")

# libuuid7 and libuuid7-dev, both replacing the former single package "uuid7".
KYR_PKG="libuuid7"
KYR_PKG_DESCRIPTION="Thread-safe monotonic binary UUIDv7 generation library"
KYR_DEB_HEADERS=(app/uuid7.h)
KYR_DEB_RUNTIME_FIELDS=("Conflicts: uuid7" "Replaces: uuid7")
KYR_DEB_DEV_FIELDS=("Conflicts: uuid7" "Replaces: uuid7")
KYR_DEB_DEV_DESCRIPTION="Development files for libuuid7 (header, static library, linker symlink)"
KYR_DEB_PC=0

# The public ABI: app/uuid7.h; the linker map's node is UUID7_<major>.
KYR_ABI_NAME="uuid7"
KYR_ABI_INCLUDE_DIR="app"
KYR_ABI_UMBRELLA="uuid7.h"
KYR_ABI_TYPE_PREFIX="uuid7_"
KYR_ABI_FUNC_PREFIX="uuid7_"
KYR_ABI_CONST_PREFIX="UUID7_"
KYR_ABI_NODE="UUID7"
