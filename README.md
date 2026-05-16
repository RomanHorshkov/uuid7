UUID7 — Compact C implementation of UUIDv7
==========================================

[![CI](https://github.com/RomanHorshkov/UUID7/actions/workflows/ci.yml/badge.svg)](https://github.com/RomanHorshkov/UUID7/actions/workflows/ci.yml)
[![Integration Tests](https://github.com/RomanHorshkov/UUID7/actions/workflows/integration-tests.yml/badge.svg)](https://github.com/RomanHorshkov/UUID7/actions/workflows/integration-tests.yml)

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

Usage

```c
#include "uuid7.h"

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
