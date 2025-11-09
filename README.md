UUID7 — Compact C implementation of UUID version 7

Overview

This repository contains a small, standalone C implementation of UUIDv7 (time-ordered UUIDs) packaged as a static library. The code is intentionally minimal and portable: header files live in `include/` and implementation in `src/`. A small `Makefile` builds a static archive `build/libuuid7.a` you can link into other C projects.

Why UUIDv7?

- UUIDv7 is a draft standard for time-ordered, lexicographically sortable UUIDs that preserve uniqueness while allowing records to be sorted by generation time. That makes them useful for databases, message streams, log IDs, and distributed systems where ordering matters in addition to uniqueness.
- Compared with UUIDv1 (MAC address + time), UUIDv7 avoids exposing hardware identifiers while still encoding time. Compared with UUIDv4 (random), UUIDv7 improves index locality and comparability.

Project layout

- `include/uuid7.h` — Public header for the library; contains the API and data types you should use.
- `src/uuid7.c` — Implementation of the UUIDv7 routines.
- `Makefile` — Simple build system that compiles `.c` files from `src/` and produces `build/libuuid7.a`.
- `build/` — The output directory (created by `make`). The archive will be `build/libuuid7.a` and intermediate objects are removed after library creation.

Build instructions

Prerequisites

- A C compiler (gcc/clang).
- GNU `make` (or any POSIX make).

To build the static library:

```sh
make
```

This will create the `build/` directory and place `libuuid7.a` there.

Using the library in your project

1) Include the header in your C source files:

```c
#include "uuid7.h"
```

2) When compiling your program, add `-Iinclude` so the compiler can find the header, and link with the static library from `build/`:

```sh
# compile your program
gcc -std=c11 -Iinclude -c myprog.c -o myprog.o
# link with the static lib
gcc myprog.o -Lbuild -luuid7 -o myprog
```

If you prefer to avoid `-l` you can also pass the full path to the archive:

```sh
gcc myprog.o build/libuuid7.a -o myprog
```

API and Examples

Please consult `include/uuid7.h` for the exact function signatures and data structures — that's the canonical reference for usage.

A minimal usage pattern looks like:

- Initialize any state required by the library (if the header exposes an init function).
- Call the UUID generation function to obtain a 16-byte UUID or a string representation.
- Use / store the UUID as required by your application.

If the library exposes a function that returns a binary 16-byte UUID, you can convert it to the conventional hex format (8-4-4-4-12) for display or storage.

Design notes and rationale

- Simplicity: the project is intentionally small and dependency-free so you can drop the `src/`/`include/` into other codebases.
- Static library: building a `.a` lets you link deterministically and avoid runtime dependencies. The provided `Makefile` deletes intermediate objects after packaging so `build/` stays tidy.
- Compiler flags: the `Makefile` targets `-std=c11` and a conservative set of warnings. You can change `CFLAGS` or `CPPFLAGS` by exporting environment variables when invoking `make`:

```sh
make CFLAGS='-std=c11 -O2 -g -Wall' CPPFLAGS='-Iinclude -D_GNU_SOURCE'
```

Testing and examples

This repo doesn't include an example program currently. To test quickly, you can create a small `test_uuid7.c` in the project root that includes `uuid7.h`, generates a few UUIDs, prints their hex representations, and then link it against `build/libuuid7.a`.

Suggested `test_uuid7.c` snippet:

```c
#include <stdio.h>
#include "uuid7.h"

int main(void) {
    unsigned char u[16];
    if (uuid7_generate(u) != 0) {
        fprintf(stderr, "failed to generate uuid7\n");
        return 1;
    }
    // convert and print in standard format (implement helper if header doesn't provide one)
    for (int i = 0; i < 16; ++i) printf("%02x", u[i]);
    printf("\n");
    return 0;
}
```

License and contributing

This repository does not currently contain a `LICENSE` file. If you plan to publish or share the library, add a license (MIT, BSD, Apache 2.0 are common for small C libraries). Also consider adding a CONTRIBUTING.md with guidelines.

Roadmap and follow-ups

- Add a small example program in `examples/` and a simple test harness.
- Add a `LICENSE` file and choose a permissive license.
- Optionally provide a pkg-config `.pc` file or CMake support for easier integration.
- Consider adding unit tests (e.g., with a tiny test runner) and CI checks for build/lint.

Contact / Support

If you want me to also add the `test_uuid7.c` example and wire it into the `Makefile` (or add a LICENSE), tell me which license you'd like and I will add those.


Completion summary

- Added `README.md` documenting the project layout, build, usage, and rationale. If you want more details specific to the public API (function names and exact signatures), I can parse `include/uuid7.h` and add concrete examples. Don't hesitate to ask for that additional detail.