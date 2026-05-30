#!/usr/bin/env bash
set -euo pipefail

# Compatibility wrapper for the stress matrix. Prefer using the explicit stages:
#   ./utils/build_stress.sh
#   ./utils/run_stress.sh

START_DIR="$(pwd -P)"
cleanup() { cd -- "${START_DIR}" >/dev/null 2>&1 || true; }
trap cleanup EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"${SCRIPT_DIR}/build_stress.sh" "$@"
"${SCRIPT_DIR}/run_stress.sh"
