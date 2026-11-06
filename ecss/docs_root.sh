#!/usr/bin/env bash
# docs_root.sh — the documentation root of the release package (ecss/README.md, "Release package"): kyr-build's pinned build root
# (build_root.sh: the same Ubuntu Base tarball and snapshot.ubuntu.com date as the release root, made without root through bwrap)
# with the package set the documents need instead of the compilers. Its own directory and identity; the release root is untouched.
#
#   docs_root.sh create | hash | verify | show | destroy
#   docs_root.sh enter [--src S] -- <command ...>   offline, S (default: the current directory) at /src
#   docs_root.sh dir                                the root's directory
#   docs_root.sh jar <dir>                          the PlantUML jar (PLANTUML_JAR), checked against its pin, copied into <dir>
#
# The PDF engine is WeasyPrint (HTML and CSS paged media: the footer is an @page margin box, a wide table gets a landscape page,
# SVG is drawn as is). Chosen in the pinned snapshot over LaTeX: +45 packages, +183 MiB installed, +41 MiB downloaded over the
# rest of the set, against +350/+108 MiB for xelatex (+208/+79 MiB for lualatex), and it reads the HTML pandoc already writes.
# Needs build/sysroot/share/kyr-build/build_root.sh (utils/deps.sh sysroot) or KYR_BUILD_ROOT_SH (create: kyr-build 2.1.6 or later);
# ECSS_DOCS_ROOT moves the root.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BR="${KYR_BUILD_ROOT_SH:-${ROOT}/build/sysroot/share/kyr-build/build_root.sh}"
export KYR_BUILD_ROOT_PACKAGES="python3,git,binutils,doxygen,graphviz,default-jre-headless,pandoc,weasyprint,librsvg2-bin,fontconfig,fonts-dejavu-core,fonts-liberation2"
PLANTUML_JAR="${PLANTUML_JAR:-/home/rh/plantuml/plantuml-mit-1.2025.10.jar}"
PLANTUML_SHA256=a0e2f8f176e812fe1118246e4ffbf2d9c2bf2a0977024b3d533459f5e2df838c   # plantuml-mit-1.2025.10.jar
die() { printf 'docs_root.sh: %s\n' "$*" >&2; exit 1; }
[[ -x "${BR}" ]] || die "no build_root.sh at ${BR} (utils/deps.sh sysroot)"
pin() { "${BR}" show 2> /dev/null | awk -v k="$1:" '$1==k && !n++ {print $2}'; }
DIR="${ECSS_DOCS_ROOT:-${XDG_CACHE_HOME:-${HOME}/.cache}/kyr-build-root/$(pin suite)-$(pin snapshot)-docs}"
cmd="${1:-}"; shift || true
case "${cmd}" in
    create)
        # fontconfig's postinst fails rootless unless build_root.sh makes /usr/local/share/fonts first (kyr-build 2.1.6)
        grep -q 'usr/local/share/fonts' "${BR}" || die "${BR} cannot install fontconfig rootless: create with kyr-build 2.1.6 or later (KYR_BUILD_ROOT_SH=<its share/build_root.sh>)"
        mkdir -p "$(dirname "${DIR}")"; exec "${BR}" create --dir "${DIR}" ;;
    hash|verify|show|destroy) exec "${BR}" "${cmd}" --dir "${DIR}" ;;
    enter) exec "${BR}" enter --dir "${DIR}" --offline "$@" ;;
    dir) printf '%s\n' "${DIR}" ;;
    jar)
        [[ -d "${1:-}" ]] || die "jar needs a directory"
        [[ -f "${PLANTUML_JAR}" ]] || die "no PlantUML jar at ${PLANTUML_JAR} (set PLANTUML_JAR; it is never downloaded)"
        have="$(sha256sum "${PLANTUML_JAR}" | awk '{print $1}')"
        [[ "${have}" == "${PLANTUML_SHA256}" ]] || die "${PLANTUML_JAR}: sha256 ${have}, pinned ${PLANTUML_SHA256}"
        cp "${PLANTUML_JAR}" "$1/" && printf '%s/%s\n' "$1" "$(basename "${PLANTUML_JAR}")" ;;
    *) sed -n '2,16p' "${BASH_SOURCE[0]}"; exit 3 ;;
esac
