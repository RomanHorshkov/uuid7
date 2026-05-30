#!/usr/bin/env bash
set -euo pipefail

# Generate a browser-readable report for a pipeline run directory.
#
# Usage:
#   ./utils/write_results_html.sh /path/to/tests/results/pipeline/runs/<run-id>
#
# Output:
#   <run-dir>/index.html

START_DIR="$(pwd -P)"
cleanup() { cd -- "${START_DIR}" >/dev/null 2>&1 || true; }
trap cleanup EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUN_RESULT_DIR="${1:-${UUID7_RESULTS_RUN_DIR:-}}"

if [[ -z "${RUN_RESULT_DIR}" ]]; then
    printf 'error: run result directory is required\n' >&2
    exit 1
fi

mkdir -p "${RUN_RESULT_DIR}"
RUN_RESULT_DIR="$(cd "${RUN_RESULT_DIR}" && pwd)"
REPORT_FILE="${RUN_RESULT_DIR}/index.html"

cd -- "${ROOT_DIR}"

html_escape() {
    sed \
        -e 's/&/\&amp;/g' \
        -e 's/</\&lt;/g' \
        -e 's/>/\&gt;/g' \
        -e 's/"/\&quot;/g'
}

escape_value() {
    printf '%s' "$1" | html_escape
}

link_href() {
    local path="$1"

    if [[ "${path}" == "${RUN_RESULT_DIR}/"* ]]; then
        printf '%s' "${path#"${RUN_RESULT_DIR}/"}"
    elif [[ "${path}" == /* ]]; then
        printf '%s' "${path}"
    else
        printf '%s' "${path}"
    fi
}

emit_file_link() {
    local path="$1"
    local label="$2"
    local href

    if [[ -n "${path}" && -e "${path}" ]]; then
        href="$(link_href "${path}")"
        printf '<a href="%s">%s</a>' \
            "$(escape_value "${href}")" \
            "$(escape_value "${label}")"
    elif [[ -n "${path}" ]]; then
        printf '<span class="missing" title="%s">%s</span>' \
            "$(escape_value "${path}")" \
            "$(escape_value "${label} missing")"
    else
        printf '<span class="muted">none</span>'
    fi
}

emit_raw_details() {
    local path="$1"
    local label="$2"

    if [[ -n "${path}" && -f "${path}" ]]; then
        printf '<details class="raw"><summary>%s</summary><pre>' \
            "$(escape_value "${label}")"
        html_escape < "${path}"
        printf '</pre></details>'
    else
        printf '<span class="muted">no raw output</span>'
    fi
}

count_rows() {
    local file="$1"

    if [[ -f "${file}" ]]; then
        awk -F '\t' '$1 !~ /^#/ && NF > 0 { count++ } END { print count + 0 }' "${file}"
    else
        printf '0\n'
    fi
}

count_status() {
    local file="$1"
    local status="$2"

    if [[ -f "${file}" ]]; then
        awk -F '\t' -v wanted="${status}" '$1 == wanted { count++ } END { print count + 0 }' "${file}"
    else
        printf '0\n'
    fi
}

latest_mtime() {
    local file="$1"

    if [[ -e "${file}" ]]; then
        stat -c '%y' "${file}" 2>/dev/null | cut -d'.' -f1
    else
        printf 'not created yet'
    fi
}

STAGE_STATUS_FILE="${RUN_RESULT_DIR}/stage_status.tsv"
ITS_SUMMARY_FILE="${RUN_RESULT_DIR}/ITs_summary.tsv"
STRESS_SUMMARY_FILE="${RUN_RESULT_DIR}/stress_summary.tsv"
COVERAGE_HTML="${RUN_RESULT_DIR}/coverage/release/ITs_release_coverage.html"
LEGEND_FILE="${RUN_RESULT_DIR}/LEGEND.md"

STAGE_COUNT="$(count_rows "${STAGE_STATUS_FILE}")"
STAGE_FAILS="$(count_status "${STAGE_STATUS_FILE}" FAIL)"
IT_COUNT="$(count_rows "${ITS_SUMMARY_FILE}")"
IT_FAILS="$(count_status "${ITS_SUMMARY_FILE}" FAIL)"
STRESS_COUNT="$(count_rows "${STRESS_SUMMARY_FILE}")"
STRESS_FAILS="$(count_status "${STRESS_SUMMARY_FILE}" FAIL)"
RUN_ID="$(basename "${RUN_RESULT_DIR}")"
GENERATED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

{
cat <<EOF
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>UUID7 Pipeline Report - $(escape_value "${RUN_ID}")</title>
<style>
:root {
  color-scheme: light;
  --bg: #f6f8fb;
  --panel: #ffffff;
  --panel-2: #f0f4f8;
  --text: #172033;
  --muted: #65738a;
  --border: #d9e1ec;
  --accent: #1d6f8f;
  --accent-2: #264f9e;
  --pass: #0f7b45;
  --fail: #b42318;
  --warn: #9a6700;
  --code: #0f172a;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  background: var(--bg);
  color: var(--text);
  font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  line-height: 1.45;
}
a { color: var(--accent-2); text-decoration: none; }
a:hover { text-decoration: underline; }
header {
  padding: 28px 32px 18px;
  background: var(--panel);
  border-bottom: 1px solid var(--border);
}
h1 { margin: 0 0 8px; font-size: 28px; letter-spacing: 0; }
.subtitle { color: var(--muted); display: flex; flex-wrap: wrap; gap: 12px 20px; }
nav {
  position: sticky;
  top: 0;
  z-index: 4;
  display: flex;
  gap: 10px;
  padding: 10px 32px;
  background: rgba(246, 248, 251, 0.95);
  border-bottom: 1px solid var(--border);
  backdrop-filter: blur(8px);
}
nav a {
  display: inline-flex;
  align-items: center;
  min-height: 32px;
  padding: 0 12px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: var(--panel);
  color: var(--text);
  font-size: 14px;
}
main { padding: 24px 32px 44px; }
section { margin: 0 0 28px; }
h2 { margin: 0 0 12px; font-size: 20px; }
.cards {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 12px;
  margin: 18px 0 0;
}
.card {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 14px;
}
.card .label { color: var(--muted); font-size: 13px; }
.card .value { margin-top: 4px; font-size: 24px; font-weight: 700; }
.card.fail .value { color: var(--fail); }
.card.pass .value { color: var(--pass); }
.panel {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 8px;
  overflow: hidden;
}
.panel-head {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  align-items: center;
  padding: 14px 16px;
  border-bottom: 1px solid var(--border);
  background: var(--panel-2);
}
.panel-head input {
  width: min(360px, 100%);
  min-height: 34px;
  padding: 6px 10px;
  border: 1px solid var(--border);
  border-radius: 6px;
  font: inherit;
}
.table-wrap { overflow: auto; }
table { width: 100%; border-collapse: collapse; min-width: 880px; }
th, td {
  padding: 10px 12px;
  border-bottom: 1px solid var(--border);
  text-align: left;
  vertical-align: top;
  font-size: 14px;
}
th {
  background: #fafcff;
  color: #344054;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: .04em;
  cursor: pointer;
  user-select: none;
}
tr:hover td { background: #fbfdff; }
.badge {
  display: inline-flex;
  align-items: center;
  min-height: 22px;
  padding: 0 8px;
  border-radius: 999px;
  font-weight: 700;
  font-size: 12px;
}
.badge.pass { color: #ffffff; background: var(--pass); }
.badge.fail { color: #ffffff; background: var(--fail); }
.badge.other { color: #ffffff; background: var(--warn); }
.metric { font-variant-numeric: tabular-nums; white-space: nowrap; }
.muted, .missing { color: var(--muted); }
.missing { border-bottom: 1px dotted var(--warn); }
details.raw { margin-top: 8px; }
details.raw summary { cursor: pointer; color: var(--accent); font-size: 13px; }
pre {
  max-height: 520px;
  overflow: auto;
  padding: 12px;
  border-radius: 6px;
  background: var(--code);
  color: #e5e7eb;
  font-size: 12px;
  line-height: 1.4;
  white-space: pre-wrap;
}
.links { display: flex; flex-wrap: wrap; gap: 8px 14px; }
.notice {
  padding: 12px 14px;
  border: 1px solid var(--border);
  border-radius: 8px;
  background: #fffaf0;
  color: #5f3d00;
}
@media (max-width: 720px) {
  header, main, nav { padding-left: 16px; padding-right: 16px; }
  nav { overflow-x: auto; }
  .panel-head { display: block; }
  .panel-head input { margin-top: 10px; width: 100%; }
}
</style>
</head>
<body>
<header>
  <h1>UUID7 Pipeline Report</h1>
  <div class="subtitle">
    <span>Run: <strong>$(escape_value "${RUN_ID}")</strong></span>
    <span>Generated: <strong>$(escape_value "${GENERATED_AT}")</strong></span>
    <span>Root: <code>$(escape_value "${RUN_RESULT_DIR}")</code></span>
  </div>
  <div class="cards">
    <div class="card"><div class="label">Stages</div><div class="value">$(escape_value "${STAGE_COUNT}")</div></div>
    <div class="card fail"><div class="label">Stage Failures</div><div class="value">$(escape_value "${STAGE_FAILS}")</div></div>
    <div class="card"><div class="label">IT Executions</div><div class="value">$(escape_value "${IT_COUNT}")</div></div>
    <div class="card fail"><div class="label">IT Failures</div><div class="value">$(escape_value "${IT_FAILS}")</div></div>
    <div class="card"><div class="label">Stress Executions</div><div class="value">$(escape_value "${STRESS_COUNT}")</div></div>
    <div class="card fail"><div class="label">Stress Failures</div><div class="value">$(escape_value "${STRESS_FAILS}")</div></div>
  </div>
</header>
<nav>
  <a href="#quick-links">Links</a>
  <a href="#stages">Stages</a>
  <a href="#integration-tests">Integration Tests</a>
  <a href="#stress-tests">Stress Tests</a>
  <a href="#raw-files">Raw Files</a>
</nav>
<main>
EOF

cat <<EOF
<section id="quick-links">
  <h2>Quick Links</h2>
  <div class="panel">
    <div class="panel-head"><strong>Open the important artifacts directly</strong></div>
    <div style="padding: 14px 16px;" class="links">
EOF
printf '      '; emit_file_link "${STAGE_STATUS_FILE}" 'stage_status.tsv'; printf '\n'
printf '      '; emit_file_link "${ITS_SUMMARY_FILE}" 'ITs_summary.tsv'; printf '\n'
printf '      '; emit_file_link "${STRESS_SUMMARY_FILE}" 'stress_summary.tsv'; printf '\n'
printf '      '; emit_file_link "${COVERAGE_HTML}" 'coverage html'; printf '\n'
printf '      '; emit_file_link "${LEGEND_FILE}" 'LEGEND.md'; printf '\n'
cat <<EOF
    </div>
  </div>
</section>
EOF

cat <<EOF
<section id="stages">
  <h2>Stages</h2>
EOF
if [[ -f "${STAGE_STATUS_FILE}" ]]; then
cat <<EOF
  <div class="panel searchable">
    <div class="panel-head"><strong>Stage Status</strong><input class="table-filter" type="search" placeholder="Filter stages, logs, status"></div>
    <div class="table-wrap"><table>
      <thead><tr><th>Status</th><th>Stage</th><th>Exit</th><th>Log</th><th>Updated</th><th>Raw</th></tr></thead>
      <tbody>
EOF
    while IFS=$'\t' read -r stage status rc log_file; do
        [[ -z "${stage}" ]] && continue
        [[ "${stage}" == \#* ]] && continue
        lower_status="$(printf '%s' "${status}" | tr '[:upper:]' '[:lower:]')"
        case "${lower_status}" in pass|fail) badge_class="${lower_status}" ;; *) badge_class="other" ;; esac
        printf '        <tr><td><span class="badge %s">%s</span></td><td>%s</td><td class="metric">%s</td><td>' \
            "$(escape_value "${badge_class}")" \
            "$(escape_value "${status}")" \
            "$(escape_value "${stage}")" \
            "$(escape_value "${rc}")"
        emit_file_link "${log_file}" 'log'
        printf '</td><td>%s</td><td>' "$(escape_value "$(latest_mtime "${log_file}")")"
        emit_raw_details "${log_file}" 'show log'
        printf '</td></tr>\n'
    done < "${STAGE_STATUS_FILE}"
cat <<EOF
      </tbody>
    </table></div>
  </div>
EOF
else
    printf '  <div class="notice">No stage status file has been generated yet.</div>\n'
fi
cat <<EOF
</section>
EOF

cat <<EOF
<section id="integration-tests">
  <h2>Integration Tests</h2>
EOF
if [[ -f "${ITS_SUMMARY_FILE}" ]]; then
cat <<EOF
  <div class="panel searchable">
    <div class="panel-head"><strong>IT Matrix</strong><input class="table-filter" type="search" placeholder="Filter profile, linkage, coverage, status"></div>
    <div class="table-wrap"><table>
      <thead><tr><th>Status</th><th>Profile</th><th>Linkage</th><th>Coverage</th><th>Result</th><th>Executable</th><th>Library</th><th>Raw Output</th></tr></thead>
      <tbody>
EOF
    while IFS=$'\t' read -r status profile linkage result_file executable library coverage; do
        [[ -z "${status}" ]] && continue
        [[ "${status}" == \#* ]] && continue
        lower_status="$(printf '%s' "${status}" | tr '[:upper:]' '[:lower:]')"
        case "${lower_status}" in pass|fail) badge_class="${lower_status}" ;; *) badge_class="other" ;; esac
        coverage_label="no"
        [[ "${coverage}" == "1" ]] && coverage_label="yes"
        printf '        <tr><td><span class="badge %s">%s</span></td><td>%s</td><td>%s</td><td>%s</td><td>' \
            "$(escape_value "${badge_class}")" \
            "$(escape_value "${status}")" \
            "$(escape_value "${profile}")" \
            "$(escape_value "${linkage}")" \
            "$(escape_value "${coverage_label}")"
        emit_file_link "${result_file}" 'result'
        printf '</td><td>'
        emit_file_link "${executable}" 'executable'
        printf '</td><td>'
        emit_file_link "${library}" 'library'
        printf '</td><td>'
        emit_raw_details "${result_file}" 'show IT output'
        printf '</td></tr>\n'
    done < "${ITS_SUMMARY_FILE}"
cat <<EOF
      </tbody>
    </table></div>
  </div>
EOF
else
    printf '  <div class="notice">No integration-test summary exists yet. Run <code>./utils/run_pipeline.sh run_ITs</code> after building ITs.</div>\n'
fi
cat <<EOF
</section>
EOF

cat <<EOF
<section id="stress-tests">
  <h2>Stress Tests</h2>
EOF
if [[ -f "${STRESS_SUMMARY_FILE}" ]]; then
cat <<EOF
  <div class="panel searchable">
    <div class="panel-head"><strong>Stress Timing Matrix</strong><input class="table-filter" type="search" placeholder="Filter profile, linkage, benchmark, status"></div>
    <div class="table-wrap"><table>
      <thead><tr><th>Status</th><th>Profile</th><th>Linkage</th><th>Benchmark</th><th>Mean ns/uuid</th><th>Mean uuid/s</th><th>Result</th><th>Executable</th><th>Library</th><th>All Stats</th></tr></thead>
      <tbody>
EOF
    while IFS=$'\t' read -r status profile linkage benchmark mean_ns_per_uuid mean_uuid_per_s result_file executable library; do
        [[ -z "${status}" ]] && continue
        [[ "${status}" == \#* ]] && continue
        lower_status="$(printf '%s' "${status}" | tr '[:upper:]' '[:lower:]')"
        case "${lower_status}" in pass|fail) badge_class="${lower_status}" ;; *) badge_class="other" ;; esac
        printf '        <tr><td><span class="badge %s">%s</span></td><td>%s</td><td>%s</td><td>%s</td><td class="metric">%s</td><td class="metric">%s</td><td>' \
            "$(escape_value "${badge_class}")" \
            "$(escape_value "${status}")" \
            "$(escape_value "${profile}")" \
            "$(escape_value "${linkage}")" \
            "$(escape_value "${benchmark}")" \
            "$(escape_value "${mean_ns_per_uuid}")" \
            "$(escape_value "${mean_uuid_per_s}")"
        emit_file_link "${result_file}" 'result'
        printf '</td><td>'
        emit_file_link "${executable}" 'executable'
        printf '</td><td>'
        emit_file_link "${library}" 'library'
        printf '</td><td>'
        emit_raw_details "${result_file}" 'show full benchmark stats'
        printf '</td></tr>\n'
    done < "${STRESS_SUMMARY_FILE}"
cat <<EOF
      </tbody>
    </table></div>
  </div>
EOF
else
    printf '  <div class="notice">No stress summary exists yet. Run <code>./utils/run_pipeline.sh build_stress</code>, then <code>./utils/run_pipeline.sh run_stress</code>.</div>\n'
fi
cat <<EOF
</section>
EOF

cat <<EOF
<section id="raw-files">
  <h2>Raw Files</h2>
  <div class="panel">
    <div class="panel-head"><strong>Run Directory Files</strong></div>
    <div class="table-wrap"><table>
      <thead><tr><th>File</th><th>Updated</th><th>Size</th></tr></thead>
      <tbody>
EOF
    while IFS= read -r -d '' file_path; do
        rel="${file_path#"${RUN_RESULT_DIR}/"}"
        size="$(wc -c < "${file_path}" 2>/dev/null || printf '0')"
        printf '        <tr><td>'
        emit_file_link "${file_path}" "${rel}"
        printf '</td><td>%s</td><td class="metric">%s bytes</td></tr>\n' \
            "$(escape_value "$(latest_mtime "${file_path}")")" \
            "$(escape_value "${size}")"
    done < <(find "${RUN_RESULT_DIR}" -type f ! -name 'index.html' -print0 | sort -z)
cat <<EOF
      </tbody>
    </table></div>
  </div>
</section>
EOF

cat <<'EOF'
</main>
<script>
(function () {
  function normalize(value) {
    return (value || '').toLowerCase();
  }

  document.querySelectorAll('.searchable').forEach(function (panel) {
    var input = panel.querySelector('.table-filter');
    var rows = Array.prototype.slice.call(panel.querySelectorAll('tbody tr'));
    if (!input) return;
    input.addEventListener('input', function () {
      var needle = normalize(input.value);
      rows.forEach(function (row) {
        row.style.display = normalize(row.textContent).indexOf(needle) === -1 ? 'none' : '';
      });
    });
  });

  document.querySelectorAll('table').forEach(function (table) {
    var headers = Array.prototype.slice.call(table.querySelectorAll('th'));
    headers.forEach(function (header, index) {
      header.addEventListener('click', function () {
        var tbody = table.querySelector('tbody');
        var rows = Array.prototype.slice.call(tbody.querySelectorAll('tr'));
        var dir = header.dataset.sortDir === 'asc' ? 'desc' : 'asc';
        header.dataset.sortDir = dir;
        rows.sort(function (a, b) {
          var av = a.children[index] ? a.children[index].textContent.trim() : '';
          var bv = b.children[index] ? b.children[index].textContent.trim() : '';
          var an = Number(av.replace(/,/g, ''));
          var bn = Number(bv.replace(/,/g, ''));
          if (!Number.isNaN(an) && !Number.isNaN(bn) && av !== '' && bv !== '') {
            return dir === 'asc' ? an - bn : bn - an;
          }
          return dir === 'asc' ? av.localeCompare(bv) : bv.localeCompare(av);
        });
        rows.forEach(function (row) { tbody.appendChild(row); });
      });
    });
  });
}());
</script>
</body>
</html>
EOF
} > "${REPORT_FILE}"

printf '[html] report ready: %s\n' "${REPORT_FILE}"
