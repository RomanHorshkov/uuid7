#!/usr/bin/env bash
set -euo pipefail

# Package smoke test: proves the .deb genuinely works for an external consumer — compiles and
# runs a tiny program against ONLY the installed system paths (/usr/local/include,
# /usr/local/lib), never the repo's own build/ tree. This is deliberately NOT a rebuild of the
# library: if this script had to compile app/uuid7.c itself, it would only prove the SOURCE
# works, not that the shipped, installed PACKAGE does.

START_DIR="$(pwd -P)"
cleanup() { cd -- "${START_DIR}"; }
trap cleanup EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd -- "${ROOT_DIR}"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"; cleanup' EXIT

if [[ ! -f /usr/local/include/uuid7.h ]]; then
    printf 'smoke_test_package: /usr/local/include/uuid7.h not found — install the .deb first\n' >&2
    exit 1
fi

cat > "${WORK_DIR}/smoke.c" <<'EOF'
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <uuid7.h>

int main(void)
{
    assert(uuid7_init(NULL, NULL) == 0);

    unsigned char a[16];
    unsigned char b[16];
    assert(uuid7_gen(a) == 0);
    assert(uuid7_gen(b) == 0);
    assert(memcmp(a, b, sizeof a) != 0); /* two generated ids must differ */
    assert(memcmp(a, b, sizeof a) < 0);  /* monotonic: a was generated first, sorts first */

    printf("smoke test: installed package generates monotonic ids correctly\n");
    return 0;
}
EOF

gcc -std=c11 -Wall -Wextra -Werror \
    -I/usr/local/include \
    "${WORK_DIR}/smoke.c" \
    -L/usr/local/lib -Wl,-rpath,/usr/local/lib -luuid7 \
    -o "${WORK_DIR}/smoke"

"${WORK_DIR}/smoke"
