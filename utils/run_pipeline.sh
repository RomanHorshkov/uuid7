#!/usr/bin/env bash
set -euo pipefail

# Orchestrate the repository build and integration-test pipeline.
#
# Local default:
#   ./utils/run_pipeline.sh
#
# Stage-oriented usage for CI:
#   ./utils/run_pipeline.sh build
#   ./utils/run_pipeline.sh build_ITs
#   ./utils/run_pipeline.sh run_ITs
#   ./utils/run_pipeline.sh build_stress
#   ./utils/run_pipeline.sh run_stress

usage() {
    cat <<'EOF'
usage: ./utils/run_pipeline.sh [all|build|build_ITs|run_ITs|build_stress|run_stress]

Default command:
  all

Environment:
  UUID7_RESULTS_RUN_ID
      Optional explicit run id. Use the same value across separate stage
      invocations if you want all logs and reports to land in one archived run.

Browser report:
  tests/results/pipeline/runs/<run-id>/index.html
EOF
}

normalize_command() {
    local raw_command="${1:-all}"

    case "${raw_command}" in
        all) printf 'all\n' ;;
        build) printf 'build\n' ;;
        build_ITs|build-its|build_its) printf 'build_ITs\n' ;;
        run_ITs|run-its|run_its) printf 'run_ITs\n' ;;
        build_stress|build-stress|build_stress_tests) printf 'build_stress\n' ;;
        run_stress|run-stress|stress) printf 'run_stress\n' ;;
        help|-h|--help) printf 'help\n' ;;
        *)
            printf 'unknown command: %s\n' "${raw_command}" >&2
            usage >&2
            exit 1
            ;;
    esac
}

record_stage_status() {
    local stage_name="$1"
    local status="$2"
    local rc="$3"
    local log_file="$4"
    local tmp_file="${STAGE_STATUS_FILE}.tmp"

    if [[ -f "${STAGE_STATUS_FILE}" ]]; then
        awk -F '\t' -v stage="${stage_name}" 'BEGIN { OFS = FS } NR == 1 || $1 != stage { print }' \
            "${STAGE_STATUS_FILE}" > "${tmp_file}"
    else
        printf '# stage\tstatus\texit_code\tlog\n' > "${tmp_file}"
    fi

    printf '%s\t%s\t%s\t%s\n' \
        "${stage_name}" \
        "${status}" \
        "${rc}" \
        "${log_file}" >> "${tmp_file}"

    mv "${tmp_file}" "${STAGE_STATUS_FILE}"
}

write_legend() {
    cat > "${LEGEND_FILE}" <<EOF
# UUID7 Pipeline Run

Run id: ${RUN_ID}
Result root: ${RUN_RESULT_DIR}

## Commands

- Full pipeline: ./utils/run_pipeline.sh
- Build libraries only: ./utils/run_pipeline.sh build
- Build integration tests only: ./utils/run_pipeline.sh build_ITs
- Run integration tests only: ./utils/run_pipeline.sh run_ITs
- Build stress matrix only: ./utils/run_pipeline.sh build_stress
- Run stress matrix only: ./utils/run_pipeline.sh run_stress

Use UUID7_RESULTS_RUN_ID=${RUN_ID} with separate stage commands to keep output in this same run directory.

## What Each Stage Means

- build: builds normal library artifacts with utils/build_libs.sh.
- build_ITs: builds test-hook-enabled libraries under build/ITs/libs and links every IT binary against both libuuid7.a and libuuid7.so for each normal GCC profile.
- run_ITs: runs the binaries listed in build/ITs/manifest.tsv and writes per-profile logs under ITs/.
- build_stress: builds stress and stress_mt against every built libuuid7.a and libuuid7.so profile; it does not run benchmarks.
- run_stress: runs the stress binaries listed in build/stress/manifest.tsv and writes timing comparison logs under stress/.

## Profile Legend

- debug: debugger-oriented, low optimization, rich debug info.
- audit: warning-heavy compiler/static-analysis profile.
- sanitize: AddressSanitizer/UBSan/LSan runtime validation profile.
- release: portable production release profile.
- native: release-like build tuned for the local CPU.
- extreme: maximum-performance local build with reduced hardening.
- tsan: ThreadSanitizer concurrency validation profile.
- release_cov: release-derived coverage-instrumented build. Coverage is generated only from release_cov/static.

## Linkage Legend

- static: executable linked against libuuid7.a.
- shared: executable linked against libuuid7.so.

## Status Legend

- PASS: command or test exited 0.
- FAIL: command or test exited non-zero. Open the referenced log/result file for details.

## Important Files

- Browser report: ${RUN_RESULT_DIR}/index.html
- Stage status: ${STAGE_STATUS_FILE}
- Library build log: ${RUN_RESULT_DIR}/build_libs.log
- IT build log: ${RUN_RESULT_DIR}/build_ITs.log
- IT run log: ${RUN_RESULT_DIR}/run_ITs.log
- IT result summary: ${RUN_RESULT_DIR}/ITs_summary.tsv
- Stress build log: ${RUN_RESULT_DIR}/build_stress.log
- Stress run log: ${RUN_RESULT_DIR}/run_stress.log
- Stress result summary: ${RUN_RESULT_DIR}/stress_summary.tsv
- Coverage HTML: ${RUN_RESULT_DIR}/coverage/release/ITs_release_coverage.html
EOF
}

write_html_report() {
    "${SCRIPT_DIR}/write_results_html.sh" "${RUN_RESULT_DIR}"
}

run_stage_with_log() {
    local stage_name="$1"
    local log_file="$2"
    local rc
    shift 2

    mkdir -p "${RUN_RESULT_DIR}"

    printf '\n[pipeline]\n'
    printf '  stage:  %s\n' "${stage_name}"
    printf '  run id: %s\n' "${RUN_ID}"
    printf '  log:    %s\n' "${log_file}"

    set +e
    "$@" 2>&1 | tee "${log_file}"
    rc="${PIPESTATUS[0]}"
    set -e

    if ((rc == 0)); then
        record_stage_status "${stage_name}" PASS "${rc}" "${log_file}"
        printf '[pipeline] %s: PASS\n' "${stage_name}"
    else
        record_stage_status "${stage_name}" FAIL "${rc}" "${log_file}"
        printf '[pipeline] %s: FAIL exit=%d\n' "${stage_name}" "${rc}"
    fi

    write_legend
    write_html_report

    return "${rc}"
}

run_build_stage() {
    run_stage_with_log build "${RUN_RESULT_DIR}/build_libs.log" \
        "${SCRIPT_DIR}/build_libs.sh"
}

run_build_its_stage() {
    run_stage_with_log build_ITs "${RUN_RESULT_DIR}/build_ITs.log" \
        "${SCRIPT_DIR}/build_ITs.sh"
}

run_run_its_stage() {
    run_stage_with_log run_ITs "${RUN_RESULT_DIR}/run_ITs.log" \
        "${SCRIPT_DIR}/run_ITs.sh"
}

run_build_stress_stage() {
    run_stage_with_log build_stress "${RUN_RESULT_DIR}/build_stress.log" \
        "${SCRIPT_DIR}/build_stress.sh"
}

run_run_stress_stage() {
    run_stage_with_log run_stress "${RUN_RESULT_DIR}/run_stress.log" \
        "${SCRIPT_DIR}/run_stress.sh"
}

print_coverage_summary() {
    local path="${RUN_RESULT_DIR}/coverage/release/coverage-summary.json"
    printf '\n[coverage]\n'
    if [[ -f "${path}" ]]; then
        python3 - "${path}" <<'PY'
import json, sys
with open(sys.argv[1]) as fh:
    d = json.load(fh)
print(f"  release  line {d['line_percent']:5.1f}% ({d['line_covered']}/{d['line_total']})"
      f"   branch {d['branch_percent']:5.1f}% ({d['branch_covered']}/{d['branch_total']})"
      f"   func {d['function_percent']:5.1f}% ({d['function_covered']}/{d['function_total']})")
for f in d["files"]:
    print(f"    {f['filename']:<20} line {f['line_percent']:5.1f}%   branch {f['branch_percent']:5.1f}%   func {f['function_percent']:5.1f}%")
PY
    else
        printf '  no coverage-summary.json found (run_ITs may not have completed)\n'
    fi
}

print_pipeline_summary() {
    local stage status rc log_file

    printf '\n[pipeline summary]\n'
    printf '  run id: %s\n' "${RUN_ID}"
    printf '  result root: %s\n' "${RUN_RESULT_DIR}"
    printf '  html report: %s\n' "${RUN_RESULT_DIR}/index.html"
    printf '  legend: %s\n' "${LEGEND_FILE}"
    printf '  stage status: %s\n' "${STAGE_STATUS_FILE}"

    while IFS=$'\t' read -r stage status rc log_file; do
        [[ -z "${stage}" ]] && continue
        [[ "${stage}" == \#* ]] && continue
        printf '  %-9s %-4s exit=%s  %s\n' \
            "${stage}" \
            "${status}" \
            "${rc}" \
            "${log_file}"
    done < "${STAGE_STATUS_FILE}"

    print_coverage_summary

    if [[ -f "${RUN_RESULT_DIR}/ITs_summary.tsv" ]]; then
        printf '\n[IT result summary]\n'
        awk -F '\t' '
            $1 !~ /^#/ && NF >= 4 {
                printf "  %-4s  %-11s %-7s  %s\n", $1, $2, $3, $4
            }
        ' "${RUN_RESULT_DIR}/ITs_summary.tsv"
    fi

    if [[ -f "${RUN_RESULT_DIR}/stress_summary.tsv" ]]; then
        printf '\n[stress result summary]\n'
        awk -F '\t' '
            $1 !~ /^#/ && NF >= 7 {
                printf "  %-4s  %-11s %-7s %-9s  %12s ns/uuid  %14s uuid/s  %s\n", $1, $2, $3, $4, $5, $6, $7
            }
        ' "${RUN_RESULT_DIR}/stress_summary.tsv"
    fi
}

START_DIR="$(pwd -P)"
cleanup() { cd -- "${START_DIR}" >/dev/null 2>&1 || true; }
trap cleanup EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd -- "${ROOT_DIR}"

COMMAND="$(normalize_command "${1:-all}")"

if [[ "${COMMAND}" == 'help' ]]; then
    usage
    exit 0
fi

RUN_ID="${UUID7_RESULTS_RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)}"
RESULT_ROOT_DIR="${ROOT_DIR}/tests/results/pipeline"
RUN_RESULT_DIR="${RESULT_ROOT_DIR}/runs/${RUN_ID}"
STAGE_STATUS_FILE="${RUN_RESULT_DIR}/stage_status.tsv"
LEGEND_FILE="${RUN_RESULT_DIR}/LEGEND.md"
export UUID7_RESULTS_RUN_ID="${RUN_ID}"
export UUID7_RESULTS_RUN_DIR="${RUN_RESULT_DIR}"

mkdir -p "${RUN_RESULT_DIR}"
if [[ "${COMMAND}" == "all" || ! -f "${STAGE_STATUS_FILE}" ]]; then
    printf '# stage\tstatus\texit_code\tlog\n' > "${STAGE_STATUS_FILE}"
fi
write_legend
write_html_report

PIPELINE_RC=0

case "${COMMAND}" in
    all)
        run_build_stage || PIPELINE_RC=$?
        if ((PIPELINE_RC == 0)); then
            run_build_its_stage || PIPELINE_RC=$?
        fi
        if ((PIPELINE_RC == 0)); then
            run_run_its_stage || PIPELINE_RC=$?
        fi
        if ((PIPELINE_RC == 0)); then
            run_build_stress_stage || PIPELINE_RC=$?
        fi
        if ((PIPELINE_RC == 0)); then
            run_run_stress_stage || PIPELINE_RC=$?
        fi
        ;;
    build)
        run_build_stage || PIPELINE_RC=$?
        ;;
    build_ITs)
        run_build_its_stage || PIPELINE_RC=$?
        ;;
    run_ITs)
        run_run_its_stage || PIPELINE_RC=$?
        ;;
    build_stress)
        run_build_stress_stage || PIPELINE_RC=$?
        ;;
    run_stress)
        run_run_stress_stage || PIPELINE_RC=$?
        ;;
esac

write_legend
write_html_report
print_pipeline_summary
exit "${PIPELINE_RC}"
