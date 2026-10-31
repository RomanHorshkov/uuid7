UUID7 — Compact C implementation of UUIDv7 ==========================================

[![Quality](https://github.com/RomanHorshkov/UUID7/actions/workflows/quality.yml/badge.svg)](https://github.com/RomanHorshkov/UUID7/actions/workflows/quality.yml)
[![Security](https://github.com/RomanHorshkov/UUID7/actions/workflows/security.yml/badge.svg)](https://github.com/RomanHorshkov/UUID7/actions/workflows/security.yml)
[![Release](https://github.com/RomanHorshkov/UUID7/actions/workflows/release.yml/badge.svg)](https://github.com/RomanHorshkov/UUID7/actions/workflows/release.yml)
[![Coverage](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/RomanHorshkov/UUID7/master/.github/badges/coverage.json)](https://github.com/RomanHorshkov/UUID7/actions/workflows/quality.yml)
[![Latest Tag](https://img.shields.io/github/v/tag/RomanHorshkov/UUID7?sort=semver)](https://github.com/RomanHorshkov/UUID7/tags)
[![Stress Suite](https://img.shields.io/badge/stress-single--thread%20%2B%20multi--thread-0a7a3e)](#stress-benchmarks)

Overview

This repository contains a small, standalone C implementation of UUIDv7 (time-ordered UUIDs). It builds both static and shared libraries and ships a heavy integration test suite with coverage reports.

Why UUIDv7?

- UUIDv7 is a time-ordered UUID format that is sortable by creation time.
- It preserves uniqueness while improving index locality compared to purely random UUIDs.

Project layout

- `app/uuid7.h` — Public header for the library.
- `app/uuid7.c` — UUIDv7 generator implementation.
- `utils/` — kyr-build's wrappers and `uuid7_project.sh` (the declarations).
- `tests/ITs/` — Integration tests.
- `tests/stress/` — Single-thread and multi-thread stress benchmarks.
- `tests/package/smoke.c` — the installed-package test.
- `build/` — Build output directory.

Build

`utils/` holds the pinned kyr-build's wrappers and `utils/uuid7_project.sh`, which declares what is built, tested and packaged (the steps are kyr-build's, its README "Wrapper contract"). No sudo; every pin of `deps.txt` lands in `build/sysroot/`.

```sh
./utils/deps.sh sysroot && eval "$(./utils/deps.sh env)" && ./utils/run_pipeline.sh   # every gate
./utils/build_libs.sh release          # build/<profile>/libuuid7.{a,so.<VERSION>} (no argument: every profile)
./utils/build_deb.sh                   # build/debs: libuuid7, libuuid7-dev, SHA256SUMS, one .buildinfo per deb
```

## Release process

Releases are tag-driven.

### Release flow

1. Make sure the branch you want to release is green locally. Run:
   ```sh
   ./utils/run_pipeline.sh
   ```

2. Bump `VERSION` if the release version changed.

3. Merge the release candidate branch into `master`.

4. Update local `master`:
   ```sh
   git checkout master
   git pull --ff-only origin master
   ```

5. Create an annotated tag that matches `VERSION`. Example for `VERSION=2.0.0`:
   ```sh
   git tag -a v2.0.0 -m "UUID7 v2.0.0"
   ```

6. Push `master`, then push the tag:
   ```sh
   git push origin master
   git push origin v2.0.0
   ```

7. GitHub Actions `release.yml` will:
   - rebuild the libraries
   - build the Debian package
   - assemble a release tarball
   - generate `SHA256SUMS`
   - publish a GitHub Release with the artifacts attached

### Important rules

- The tag must match `VERSION` exactly. If `VERSION` is `2.0.0`, the tag must be `v2.0.0`.
- Release tags should always be annotated tags, not lightweight tags.
- Do not tag from a branch that is not the exact code you want on `master`.

### Published release artifacts

Each GitHub Release publishes:

- `uuid7-<version>-linux-x86_64.tar.gz`
- `libuuid7_<version>_<arch>.deb` — runtime: `libuuid7.so.<version>` + soname symlink
- `libuuid7-dev_<version>_<arch>.deb` — development: `uuid7.h`, `libuuid7.a`, `libuuid7.so` linker symlink; depends on `libuuid7 (= <version>)`. Both replace the former `uuid7` package.
- `SHA256SUMS`

Testing (heavy + coverage)

```sh
./utils/build_ITs.sh                   # the integration suites against every profile's archive and shared object, test hooks
                                       # (-DUUID7_TESTING) in its libraries only, then coverage: tests/results/ITs/coverage
./utils/build_stress.sh && ./utils/run_stress.sh   # every profile x static/shared: tests/results/stress/stress_summary.tsv
./utils/smoke_test_package.sh          # tests/package/smoke.c against the installed debs only
```

The pipeline's `package-smoke` stage runs the smoke test against the debs extracted into `build/smoke`; CI runs it after installing them. The integration suites are `tests/ITs/integration_test.c` (layout, order, import, bounds, threads) and `tests/ITs/faults_test.c` (getrandom interrupted, short, empty or failing, the `/dev/urandom` fallback failing, clock_gettime failing: the test defines those calls itself, forwarding to libc unless a test scripts a failure). The coverage badge is the integration coverage number, written by kyr-build's `coverage_badge.py`.

Usage

```c
#include "uuid7.h"

/* Optional: restore monotonic state from the last persisted UUIDv7. */
if (uuid7_init(NULL, last_uuid) != 0) {
    /* handle invalid imported UUID */
}

uint8_t u[UUID7_SIZE_BYTES];
if (uuid7_gen(u) != 0) {
    /* handle error */
}
```

Abuse bounds
------------

The generator's logical clock may run at most `UUID7_MAX_AHEAD_MS` (10 s by
default, `-DUUID7_MAX_AHEAD_MS=<ms>` to override) ahead of the wall clock.
Sustained generation faster than 4096 ids/ms borrows future milliseconds up to
that bound, then `uuid7_gen()` returns `-3` without touching the state until
real time catches up. A persisted floor dated beyond the bound — a poisoned
import or a runaway clock — is refused by `uuid7_raise_floor()`/`uuid7_init()`
with `-4`. Together these guarantee the 48-bit timestamp field never wraps and
no caller can date the process's ids arbitrarily into the future.

Compile locally against the built library:

```sh
gcc -std=c11 -Iapp -c myprog.c -o myprog.o
gcc myprog.o -Lbuild/release -Wl,-rpath,'$ORIGIN' -luuid7 -o myprog
```

License

MIT — see [LICENSE](LICENSE). Every built .deb ships the same terms at
/usr/share/doc/uuid7/copyright (generated from debian/copyright).

## Build profiles & hardening

Builds go through `utils/build_libs.sh [profile ...]`, driven by the profile catalog of the pinned `kyr-build` package (`deps.txt`; `utils/gcc_build_profiles.sh` and `utils/check_hardening.sh` are thin wrappers over `build/sysroot/share/kyr-build/`, never copies); artifacts land in `build/<profile>/`; `utils/check_hardening.sh` gates every release artifact.

| Profile | Optimization | Warnings | Instrumentation | Hardened | Use it for |
|---|---|---|---|---|---|
| debug | `-Og -g3` | core | — | no | day-to-day development |
| audit | `-O1 -g3` | everything + `-fanalyzer` | — | yes | compiler-driven validation |
| sanitize | `-O1 -g3` | strict | ASan+UBSan+LSan | yes minus FORTIFY — conflicts with ASan | runtime bug hunting |
| release | `-O2 -DNDEBUG` | strict | — | yes — full set below | production / the deb payload |
| native | `-O3 -flto -march=native` | strict | — | yes | benchmarks on the deploy box |
| extreme | `-O3 -flto -march=native` | core | — | deliberately none | max-perf experiments only |

Release hardening by stage:

| Flag | Stage | Purpose |
|---|---|---|
| `-fstack-protector-strong` | compile | stack canary on frames with arrays / address-taken locals |
| `-fstack-clash-protection` | compile | page-by-page stack growth — the guard page can't be jumped |
| `-fcf-protection=full` | compile | x86-64 CET: indirect-branch tracking + shadow stack, NOP on older CPUs |
| `-D_FORTIFY_SOURCE=3` | preprocess | checked libc calls with dynamic object sizes |
| `-fPIC` | compile | position-independent code — libraries |
| `-Wl,-z,relro -Wl,-z,now` | link | GOT/PLT read-only after load — full RELRO |
| `-Wl,-z,noexecstack` | link | non-executable stack asserted |
| `-Wl,-z,defs` | link .so | undefined symbols fail the build not the load |

Build from a clean clone: `./utils/deps.sh sysroot && eval "$(./utils/deps.sh env)" && ./utils/run_pipeline.sh` (no sudo; every pin of `deps.txt` lands in `build/sysroot/`). Diagram: `docs/diagrams/tooling_sequence.puml`.

ABI: `abi/layout.txt` is the committed public ABI of `app/uuid7.h` (layouts, enumerator values, callbacks, function signatures, numeric constants), checked by kyr-build's gate through `utils/check_abi.sh` (pipeline stages `abi` and `abi-exports`): a changed line needs a raised major, a new public name a raised minor, and `libuuid7.so` exports exactly the declared functions as `<name>@@UUID7_<major>` (`utils/uuid7.map`); `utils/uuid7_project.sh` says what is public. Debs through kyr-build's `lib/deb.sh` (`X-Kyr-Tier`, byte-reproducible). Diagram: `docs/diagrams/abi_exports.puml`.
