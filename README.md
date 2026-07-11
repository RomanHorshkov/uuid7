UUID7 — Compact C implementation of UUIDv7
==========================================

[![Deb Package](https://github.com/RomanHorshkov/UUID7/actions/workflows/ci.yml/badge.svg)](https://github.com/RomanHorshkov/UUID7/actions/workflows/ci.yml)
[![Build + Tests](https://github.com/RomanHorshkov/UUID7/actions/workflows/integration-tests.yml/badge.svg)](https://github.com/RomanHorshkov/UUID7/actions/workflows/integration-tests.yml)
[![Release](https://github.com/RomanHorshkov/UUID7/actions/workflows/release.yml/badge.svg)](https://github.com/RomanHorshkov/UUID7/actions/workflows/release.yml)
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
- `utils/` — Build, packaging, test, and coverage scripts.
- `tests/ITs/` — Integration tests.
- `tests/stress/` — Single-thread and multi-thread stress benchmarks.
- `build/` — Build output directory.

Build

Static + shared libraries:

```sh
./utils/make_libs.sh
```

Artifacts:

- `build/libuuid7.a`
- `build/libuuid7.so.<VERSION>`

Build Debian package:

```sh
./utils/make_deb.sh
```

Release process

Releases are tag-driven. See [RELEASING.md](./RELEASING.md) for the exact
merge, tag, and publish flow.

Testing (heavy + coverage)

The integration test script runs the most expensive tests by default and always generates coverage reports.

Requirements (Ubuntu/Debian):

```sh
sudo apt install libcmocka-dev gcovr
```

Run:

```sh
./utils/make_ITs.sh
```

Coverage outputs:

- `tests/results/ITs/ITs_all_coverage.html`
- `tests/results/ITs/ITs_all_coverage.xml`
- `tests/results/ITs/coverage-summary.json`

Stress Benchmarks

The stress script builds the library first, links dedicated benchmark binaries
against the produced static library, and writes human-readable result files.

Run:

```sh
./utils/make_stress.sh
```

Benchmark outputs:

- `tests/results/stress/stress_result.txt`
- `tests/results/stress/stress_mt_result.txt`

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

Compile locally against the built library:

```sh
gcc -std=c11 -Iapp -c myprog.c -o myprog.o
gcc myprog.o -Lbuild -luuid7 -o myprog
```

License

No license file yet. Add one (MIT/BSD/Apache-2.0) before distributing.

## Build profiles & hardening

Builds go through `utils/build_libs.sh [profile ...]`, driven by the shared
catalog `utils/gcc_build_profiles.sh` (synced verbatim from
`Utils/compilation/`, never edited locally); artifacts land in
`build/<profile>/`; `utils/check_hardening.sh` gates every release artifact.

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
