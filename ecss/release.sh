#!/usr/bin/env bash
# release.sh <ref> --baseline <name> [--out DIR] — the release package of uuid7 (ecss/README.md, "Release package"). A clean clone
# of the ref's commit in build/tmp (only its history and its own tag: nothing of the working tree), the frozen baseline
# ecss/baseline/<name>/ checked against that commit (ecss.py baseline: refused when missing, changed, of another commit or of
# another product) and its evidence unpacked; then, inside the documentation root (ecss/docs_root.sh, offline), make.sh --docs-only
# against that export and evidence and ecss/tools/package.py: each document in PDF, DOCX and ODT, the site, the package index and
# SHA256SUMS, into build/docs/<name>/ (DIR). SOURCE_DATE_EPOCH is the commit's time; a tag ref names the release's tag, any other
# ref is an untagged build. A second build of the same commit must give the same SHA256SUMS: it is compared with the package it
# replaces and exit 1 lists every file that differs. The deb cache is only read (a private copy of the pinned kyr-build deb).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export TMPDIR="${HERE}/build/tmp"
die() { printf 'release.sh: %s\n' "$*" >&2; exit 1; }
say() { printf '== %s\n' "$*"; }
ref="${1:-}"
[[ -n "${ref}" && "${ref}" != -* ]] || die "usage: release.sh <ref> --baseline <name> [--out DIR]"
shift
name=""; out=""
while (( $# )); do
    case "$1" in
        --baseline) name="${2:-}"; shift 2 ;;
        --out) out="${2:-}"; shift 2 ;;
        *) die "unknown option $1" ;;
    esac
done
[[ "${name}" =~ ^[A-Za-z0-9._-]+$ ]] || die "--baseline <name> is required: a baseline frozen under ecss/baseline/"
commit="$(git -C "${HERE}" rev-parse --verify --quiet "${ref}^{commit}")" || die "${ref} is not a commit of ${HERE}"
tag=""
git -C "${HERE}" rev-parse --verify --quiet "refs/tags/${ref}" > /dev/null && tag="${ref}"
out="$(realpath -m "${out:-${HERE}/build/docs/${name}}")"
W="${TMPDIR}/release-${name}"; S="${W}/src"
rm -rf "${W}"; mkdir -p "${W}/cache/debs"
t0=${SECONDS}

say "clone: ${commit}${tag:+, tag ${tag}}"
git clone --quiet --no-tags --no-checkout "${HERE}" "${S}"
git -C "${S}" checkout --quiet --detach "${commit}"
[[ -z "${tag}" ]] || git -C "${S}" fetch --quiet --no-tags "${HERE}" "refs/tags/${tag}:refs/tags/${tag}"
git -C "${S}" remote remove origin
git -C "${S}" for-each-ref --format='%(refname:short)' refs/heads | xargs -r git -C "${S}" branch -q -D

say "kyr-build: the commit's pin (utils/deps.sh sysroot from a private copy of the cached deb)"
read -r kver < <(awk '$1=="own" && $2=="kyr-build" {print $3}' "${S}/deps.txt") || die "deps.txt pins no kyr-build"
cache="${KYR_DEPS_CACHE:-${RH_DEPS_CACHE:-${HOME}/.cache/rh-deps}}"
cp "${cache}/debs/kyr-build_${kver}_all.deb" "${W}/cache/debs/" 2> /dev/null || die "kyr-build ${kver} is not in ${cache}/debs (utils/deps.sh fetch)"
( cd "${S}" && KYR_DEPS_CACHE="${W}/cache" ./utils/deps.sh sysroot > "${W}/deps.txt" 2>&1 ) || { tail -n 5 "${W}/deps.txt"; die "deps.sh sysroot failed"; }
export KYR_DEPS_CACHE="${W}/cache"   # build_root.sh enter binds the deb cache: this copy, never the shared one

say "documentation root: $("${S}/ecss/docs_root.sh" dir)"
"${S}/ecss/docs_root.sh" verify || die "the documentation root does not match its record (ecss/docs_root.sh create)"
mkdir -p "${S}/build/tools"
jar="$("${S}/ecss/docs_root.sh" jar "${S}/build/tools")" || die "no PlantUML jar of the pinned sha256"
snapshot="$("${S}/build/sysroot/share/kyr-build/build_root.sh" show | awk '$1=="snapshot:" {print $2}')"

epoch="$(git -C "${S}" log -1 --format=%ct "${commit}")"
export SOURCE_DATE_EPOCH="${epoch}" ECSS_RELEASE="${name}" ECSS_RELEASE_TAG="${tag}" ECSS_RMS_EXPORT="/src/ecss/baseline/${name}" \
    PLANTUML_JAR="/src/build/tools/$(basename "${jar}")" ECSS_DOCS_SNAPSHOT="${snapshot}"
export KYR_BUILD_ROOT_ENV="ECSS_RELEASE ECSS_RELEASE_TAG ECSS_RMS_EXPORT PLANTUML_JAR ECSS_DOCS_SNAPSHOT"

say "baseline: ecss/baseline/${name}"
"${S}/ecss/docs_root.sh" enter --src "${S}" -- python3 -B ecss/tools/ecss.py baseline "${name}" --unpack || die "the release is refused"

say "documents: make.sh --docs-only inside the documentation root"
"${S}/ecss/docs_root.sh" enter --src "${S}" -- bash ecss/make.sh --docs-only > "${W}/make.txt" 2>&1 \
    || { tail -n 25 "${W}/make.txt"; die "make.sh --docs-only is RED (${W#${HERE}/}/make.txt)"; }
grep -E '^   (pipeline|traceability|evidence|configuration flag|diagrams)' "${W}/make.txt" || true

say "package: ecss/tools/package.py (pandoc, WeasyPrint)"
"${S}/ecss/docs_root.sh" enter --src "${S}" -- python3 -B ecss/tools/package.py "build/docs/${name}" > "${W}/package.txt" 2>&1 \
    || { tail -n 25 "${W}/package.txt"; die "the package is incomplete (${W#${HERE}/}/package.txt)"; }
tail -n 1 "${W}/package.txt"

prev=""
[[ -f "${out}/SHA256SUMS" && "$(cat "${out}.commit" 2> /dev/null)" == "${commit}" ]] && prev="$(cat "${out}/SHA256SUMS")"
rm -rf "${out}"; mkdir -p "$(dirname "${out}")"
cp -a "${S}/build/docs/${name}" "${out}"
printf '%s\n' "${commit}" > "${out}.commit"   # the commit of this build, which the next build of the release compares with
( cd "${out}" && sha256sum --quiet -c SHA256SUMS ) || die "SHA256SUMS does not match the files"
say "release ${name}: ${out#${HERE}/} ($(( SECONDS - t0 )) s), SHA256SUMS $(sha256sum "${out}/SHA256SUMS" | cut -c1-64)"
if [[ -n "${prev}" ]]; then
    if [[ "${prev}" == "$(cat "${out}/SHA256SUMS")" ]]; then
        say "reproducible: identical to the previous build of commit ${commit:0:12}"
    else
        diff <(printf '%s\n' "${prev}") "${out}/SHA256SUMS" | sed -n 's/^[<>] [0-9a-f]*  /   differs: /p' | sort -u
        die "not reproducible: the previous build of commit ${commit:0:12} had another SHA256SUMS"
    fi
fi
