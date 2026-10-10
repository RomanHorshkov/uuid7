#!/usr/bin/env bash
# make.sh [--docs-only | --serve [port]] — the one click of the ECSS pipeline (ecss/README.md): every gate of the repository, the evidence ECSS asks
# for, the diagrams, the documents (build/ecss/md) and one site (build/ecss/html/index.html). Idempotent; no network once the pins of
# deps.txt are cached. --docs-only keeps the last run's test results and evidence and regenerates the documents and the site.
# The gates and the packages run inside the pinned release root when it exists (ECSS_HOST=1: on the host); the evidence records
# the commit it is of and the toolchain that produced it.
# Exit 1 when a gate failed (the documents are written anyway and say what failed).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}" || exit 1
export TMPDIR="${ROOT}/build/tmp"
OUT="${ROOT}/build/ecss"
EV="${OUT}/evidence"
DOCS_ONLY=0
[[ "${1:-}" == --docs-only ]] && DOCS_ONLY=1
# --serve: the built site over http on loopback (a sandboxed browser, such as the Flatpak Firefox, cannot follow file:// links
# out of the one file it was handed; over http every link works)
if [[ "${1:-}" == --serve ]]; then
    [[ -f "${OUT}/html/index.html" ]] || { printf 'make.sh: no site yet: run ecss/make.sh first\n' >&2; exit 1; }
    port="${2:-8790}"
    printf 'serving %s on http://127.0.0.1:%s/  (Ctrl-C stops)\n' "${OUT}/html" "${port}"
    exec python3 -m http.server --bind 127.0.0.1 --directory "${OUT}/html" "${port}"
fi
say() { printf '== %s\n' "$*"; }
need() { command -v "$1" > /dev/null 2>&1 || { printf 'make.sh: %s not found (%s)\n' "$1" "$2" >&2; exit 1; }; }
need python3 "the generator"
need doxygen "the site"
need dot "doxygen's graphs: graphviz"
mkdir -p "${TMPDIR}" "${OUT}"
rc=0

if (( ! DOCS_ONLY )); then
    rm -rf "${OUT}/puml" "${EV}"
    mkdir -p "${EV}"
    say "sysroot: utils/deps.sh sysroot"
    ./utils/deps.sh sysroot > "${EV}/deps.txt" 2>&1 || { tail -n 5 "${EV}/deps.txt"; exit 1; }
    eval "$(grep '^export' "${EV}/deps.txt")"
    # The evidence is produced inside the pinned release root (build_root.sh enter, as releases are built) when it exists;
    # else on the host, which the documents then call a host build, not the release environment. ECSS_HOST=1 forces the host.
    root_sh="${ROOT}/build/sysroot/share/kyr-build/build_root.sh"
    in_env() { "$@"; }
    if [[ "${ECSS_HOST:-0}" != 1 && -x "${root_sh}" ]] && "${root_sh}" enter -- true > /dev/null 2>&1; then
        in_env() { "${root_sh}" enter -- "$@"; }
        say "environment: the release root ($("${root_sh}" enter -- printenv KYR_BUILD_ROOT_ID))"
    else
        say "environment: host build, not the release environment"
        # the format stage wants clang-format 18 (the pinned major): a host without it borrows the release root's
        if ! command -v clang-format-18 > /dev/null 2>&1 && ! clang-format --version 2> /dev/null | grep -q 'version 18\.' \
            && [[ -x "${root_sh}" ]] && "${root_sh}" enter -- clang-format-18 --version > /dev/null 2>&1; then
            mkdir -p "${TMPDIR}/cf18-root"
            printf '#!/usr/bin/env bash\nexec %q enter -- clang-format-18 "$@"\n' "${root_sh}" > "${TMPDIR}/cf18-root/clang-format-18"
            chmod 0755 "${TMPDIR}/cf18-root/clang-format-18"
            export PATH="${TMPDIR}/cf18-root:${PATH}"
            say "format: clang-format 18 from the release root"
        fi
    fi
    # what the evidence is of (the commit and its state) and what produced it (read where it runs, never assumed)
    { printf 'commit: %s\nstate: %s\nbranch: %s\ndescribe: %s\nat: %s\n' "$(git rev-parse HEAD)" \
          "$([[ -z "$(git status --porcelain --untracked-files=no)" ]] && echo clean || echo 'uncommitted changes')" \
          "$(git rev-parse --abbrev-ref HEAD)" "$(git describe --tags --always --dirty)" "$(date '+%Y-%m-%d %H:%M:%S')"; } > "${EV}/configuration.txt"
    in_env bash -c '
        [[ -n "${KYR_BUILD_ROOT_ID:-}" ]] && echo "environment: release root ${KYR_BUILD_ROOT_ID}" || echo "environment: host build, not the release environment"
        . /etc/os-release; echo "os: ${PRETTY_NAME}"
        echo "gcc: $(gcc -dumpfullversion)"
        echo "glibc: $(ldd --version | head -n 1 | awk "{print \$NF}")"
        echo "binutils: $(ld --version | head -n 1 | awk "{print \$NF}")"
        echo "cmocka: $(pkg-config --modversion cmocka 2> /dev/null || dpkg-query -W -f "\${Version}" libcmocka-dev 2> /dev/null || echo unknown)"
        echo "gcovr: $(gcovr --version 2> /dev/null | head -n 1 | awk "{print \$NF}")"
        echo "clang-format: $( (clang-format-18 --version || clang-format --version) 2> /dev/null | head -n 1 | grep -o "version [0-9.]*" | cut -d" " -f2)"
        echo "python3: $(python3 --version | cut -d" " -f2)"
        echo "kernel: $(uname -sr) (the host kernel either way)"' > "${EV}/toolchain.txt" 2>&1
    say "pipeline: utils/run_pipeline.sh (gates, six profiles, suites and scenarios, coverage, stress, packages)"
    in_env ./utils/run_pipeline.sh > "${EV}/pipeline.txt" 2>&1 || rc=1
    tail -n 1 "${EV}/pipeline.txt"
    say "evidence: packages built again (reproducibility), hardening of the release library"
    cp build/debs/SHA256SUMS "${EV}/SHA256SUMS.first" 2> /dev/null || : > "${EV}/SHA256SUMS.first"
    # the second build is made where the first was: inside the root a release build, on the host a development build (a release
    # is refused there); a refused or failed build is reported as such, never as different bytes
    if ! KYR_ALLOW_HOST_BUILD=1 in_env ./utils/build_deb.sh > "${EV}/build_deb_again.txt" 2>&1; then verdict="different (the second build failed: build_deb_again.txt)"
    elif [[ ! -s "${EV}/SHA256SUMS.first" ]]; then verdict="different (no first build)"
    elif cmp -s build/debs/SHA256SUMS "${EV}/SHA256SUMS.first"; then verdict=identical
    else verdict=different; fi
    { printf 'first build (pipeline stage package):\n'; cat "${EV}/SHA256SUMS.first"
      printf 'second build (utils/build_deb.sh):\n'; cat build/debs/SHA256SUMS 2> /dev/null; printf 'SHA256SUMS %s\n' "${verdict}"; } \
        > "${EV}/reproducible.txt"
    in_env ./utils/check_hardening.sh build/release/libuuid7.so build/release/libuuid7.a > "${EV}/hardening.txt" 2>&1 || rc=1
fi

say "documents: ecss/tools/ecss.py"
rm -rf "${OUT}/md" "${OUT}/html" "${OUT}/svg" "${OUT}/xml_api" "${OUT}/rms"
python3 ecss/tools/ecss.py components-dox || exit 1
python3 ecss/tools/ecss.py api-doxyfile | doxygen - > /dev/null 2>&1
# exit 3: the rms export is refused (a decision or a baseline without its actor): RED, the documents and the site say why
python3 ecss/tools/ecss.py generate; g=$?
(( g == 3 )) && rc=1
(( g == 0 || g == 3 )) || exit 1
mkdir -p "${OUT}/svg"

say "site: doxygen ecss/Doxyfile ($(doxygen --version), $(dot -V 2>&1 | awk '{print $1, $5}'))"
ECSS_VERSION="$(tr -d '[:space:]' < VERSION)" ECSS_ROOT="${ROOT}" doxygen ecss/Doxyfile || exit 1
mkdir -p "${OUT}/html/evidence" "${OUT}/html/diagrams"
cp -r "${EV}/." "${OUT}/html/evidence/" 2> /dev/null
[[ -d tests/results/logs/it ]] && cp -r tests/results/logs/it "${OUT}/html/evidence/logs"
[[ -d tests/results/ITs/coverage ]] && cp -r tests/results/ITs/coverage "${OUT}/html/evidence/coverage"
cp "${OUT}"/puml/*.puml "${OUT}/html/diagrams/" 2> /dev/null
cp "${OUT}"/svg/*.svg "${OUT}/html/diagrams/" 2> /dev/null

python3 - "${OUT}" << 'PY'
import json, sys
s = json.load(open(sys.argv[1] + "/summary.json"))
w = sum(1 for l in open(sys.argv[1] + "/doxygen_warnings.txt") if l.strip())
print("   %d requirements (%d SRS, %d ICD), %d verified; %d tests, %d scenarios; %d documents; doxygen warnings: %d"
      % (s["requirements"], s["srs"], s["icd"], s["verified"], s["tests"], s["scenarios"], s["documents"], w))
print("   pipeline: %s; source of truth: %s" % (("GREEN" if s["pipeline_green"] else "FAILED") if s["pipeline_ran"] else "not run",
                                             s["mode"] + (" REFUSED (RED)" if s["refusal"] else "")))
if s["refusal"]:
    print("   RED: " + s["refusal"])
print("   evidence: commit %s (%s), %s; release tag %s" % (s["evidence_commit"][:12] or "not recorded", s["evidence_state"] or "?",
                                                         s["environment"], s["tag"]))
for f in s["config_flags"]:
    print("   configuration flag: " + f)
print("   claim: %s (open items: %d)" % (s["claim"], s["open_items"]))
if s["render_note"]:
    print("   " + s["render_note"])
PY
printf 'markdown: %s\nsite:     %s\nopen it:  bash ecss/make.sh --serve   (then http://127.0.0.1:8790/)\n' "${OUT}/md" "${OUT}/html/index.html"
exit "${rc}"
