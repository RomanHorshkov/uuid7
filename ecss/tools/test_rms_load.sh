#!/usr/bin/env bash
# test_rms_load.sh — build/ecss/rms/load.sh against a copy of the scripted story's local rms database (rms_tests/local/build.sh, never
# the box): as the verification engineer, every result of this refresh is recorded against the full commit the evidence is of and
# names results.tsv by its sha256. The database is copied into build/tmp; the original is never written. SKIP without kyr-rms-cli
# (KYR_RMS_CLI, its libraries KYR_RMS_LIBS) or the story's database (RMS_STORY_DB, its uids RMS_STORY_UIDS). Exit 1 on a failure.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLONE="$(dirname "${ROOT}")"
CLI="${KYR_RMS_CLI:-${CLONE}/kyr_rms/build/release/kyr-rms-cli}"
LIBS="${KYR_RMS_LIBS:-${CLONE}/kyr_rms/build/sysroot/lib}"
STORY="${RMS_STORY_DB:-${CLONE}/rms_tests/db}"
UIDS="${RMS_STORY_UIDS:-${CLONE}/rms_tests/work/uids.json}"
RMS="${ROOT}/build/ecss/rms"
skip() { printf 'test_rms_load: SKIP (%s)\n' "$*"; exit 0; }
fail() { printf 'test_rms_load: FAIL: %s\n' "$*"; exit 1; }
[[ -x "${CLI}" ]] || skip "no kyr-rms-cli at ${CLI}"
[[ -d "${STORY}" && -f "${UIDS}" ]] || skip "no story database at ${STORY}"
[[ -x "${RMS}/load.sh" ]] || fail "no ${RMS}/load.sh: run ecss/make.sh"
export LD_LIBRARY_PATH="${LIBS}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
ver="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["ver"])' "${UIDS}")"
commit="$(awk '$1=="commit:" {print $2}' "${ROOT}/build/ecss/evidence/configuration.txt")"
[[ "${commit}" =~ ^[0-9a-f]{40}$ ]] || fail "no evidence commit in build/ecss/evidence/configuration.txt"
W="${ROOT}/build/tmp/test_rms_load"
rm -rf "${W}"; mkdir -p "${W}"
cp -a "${STORY}" "${W}/db"
KYR_RMS_CLI="${CLI}" "${RMS}/load.sh" "${W}/db" "${ver}" > "${W}/load.txt" 2>&1 || { tail -n 5 "${W}/load.txt"; fail "load.sh refused a result"; }
sha="$(sha256sum "${RMS}/results.tsv" | cut -d' ' -f1)"
want=0; bad=0
while IFS=$'\t' read -r c v _; do
    [[ "${v}" == not-run ]] && continue
    want=$((want + 1))
    "${CLI}" "${W}/db" results "${c}" --project UUID7 --actor "${ver}" --json 2> /dev/null > "${W}/r.json"
    python3 - "${W}/r.json" "${v}" "${commit}" "${sha}" << 'PY' || { bad=$((bad + 1)); printf '  %s: the latest result is not this run against %s\n' "${c}" "${commit:0:12}"; }
import json, sys
r = json.load(open(sys.argv[1]))[-1]
ok = r["status"] == sys.argv[2] and ("commit %s," % sys.argv[3]) in r["env"] and r["evidence"].endswith("sha256:" + sys.argv[4])
sys.exit(0 if ok else 1)
PY
done < "${RMS}/results.tsv"
(( bad == 0 )) || fail "${bad} of ${want} result(s) not recorded against the full commit"
printf 'test_rms_load: %d result(s) recorded against commit %s with results.tsv sha256 %s (%s)\n' "${want}" "${commit}" "${sha:0:12}" \
    "$(grep '^results recorded' "${W}/load.txt")"
rm -rf "${W}"
