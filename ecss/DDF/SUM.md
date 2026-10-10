# Software user manual (SUM) {#UUID7_DOC_SUM}

<!-- ecss:docinfo id=UUID7-DOC-SUM -->

## 1 Introduction

This SUM tells a developer how to build against uuid7, call it, handle its errors and keep identifiers ordered across restarts. The user of a library is the programmer who links it; there is no operator. It is released at CDR and updated for QR and AR (ECSS-E-ST-40 5.5.2.8, 5.6.3.3, 5.6.4.3).

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Conventions

`monospace` is code, a file or a command; `uuid7_gen()` names a function; `$` starts a shell command; return values are written as the integers the functions return.

## 5 Purpose of the Software

uuid7 issues 16-byte identifiers (RFC 9562 UUIDv7) that sort by creation time: unique within a process, strictly increasing even when the wall clock stalls or steps back, and continuing after a restart when the newest stored identifier is imported. Use them as database keys that append in order, without a central counter or a lock.

## 6 External view of the software

| File | Package | Needed for |
|---|---|---|
| `/usr/local/include/uuid7.h` | libuuid7-dev | compiling |
| `/usr/local/lib/libuuid7.a` | libuuid7-dev | static linking |
| `/usr/local/lib/libuuid7.so` (link) | libuuid7-dev | dynamic linking |
| `/usr/local/lib/libuuid7.so.1`, `libuuid7.so.<version>` | libuuid7 | running a dynamically linked program |

The library keeps no file and no data at rest; it reads `/dev/urandom` only when `getrandom(2)` fails. Security and privacy: an identifier discloses its creation time to the millisecond; nothing else of the process.

## 7 Operations environment

### 7.1 General

A C11 compiler, a Linux system with glibc, and the two packages; nothing else (no daemon, no configuration file, no environment variable).

### 7.2 Hardware configuration

Any x86-64 machine; memory: 24 bytes of static data and about 1.7 KB of code (\ref UUID7_DOC_SDD "SDD" <4.6>).

### 7.3 Software configuration

<!-- ecss:diagram name=package_uuid7 -->

### 7.4 Operational constraints

There are no modes. Every function may be called at any time from any thread. Degraded situations: a failed clock read still yields unique identifiers with timestamp 0 (watch uuid7_clock_failure_count()); a failed entropy source makes uuid7_gen() return -2 until it recovers.

## 8 Operations basics

The tasks of a program using the library: at start, optionally choose the RNG and import the newest stored identifier; then generate identifiers on any thread; there is no shutdown task.

## 9 Operations manual

### 9.1 General

One profile: the program that links the library. Its elementary operations are the five functions of <10.4>.

### 9.2 Set-up and initialisation

Install the packages released for the version you pin (the platform's way: an `own libuuid7` and `own libuuid7-dev` line in your `deps.txt`, with the exact version and SHA-256, then `./utils/deps.sh sysroot` or `install`). Or build from the source: `./utils/deps.sh sysroot && ./utils/build_libs.sh release` (outputs in `build/release/`). Compile with `-I/usr/local/include` and link with `-luuid7`. To change the ahead bound, rebuild with `-DUUID7_MAX_AHEAD_MS=<ms>` (the packaged library uses 10000).

### 9.3 Getting started

~~~{.c}
#include <uuid7.h>

uint8_t id[UUID7_SIZE_BYTES];
if (uuid7_gen(id) != 0) { /* back off, see 9.7 */ }
~~~

No initialisation is needed: the first call installs the built-in RNG. Check-list when it fails: -1 the buffer is NULL; -2 the entropy source failed; -3 generation outran the wall clock by `UUID7_MAX_AHEAD_MS`, or the clock is wrong.

### 9.4 Access

No password, no access control: the library is linked into your process. Identifiers are not secrets: do not use them as access tokens or capabilities (RFC 9562 6.9); their timestamp is readable by anyone who holds one.

### 9.5 Normal operations

1. At start (once, before workers generate): `uuid7_init(NULL, newest_stored_id)` imports the newest identifier you already stored (or pass NULL), or, when the store opens later, `uuid7_raise_floor(newest_stored_id)` then.
2. Generate: `uuid7_gen(buf)` on any thread; store or send the 16 bytes as they are: `memcmp` order is generation order.
3. Optionally replace the RNG: `uuid7_set_rng_func(my_rng)`; `uuid7_set_rng_func(NULL)` restores the built-in one.
4. Health: poll `uuid7_clock_failure_count()`; alert if it is ever non-zero.

### 9.6 Normal termination

Nothing to do: the library holds no resource. To continue the order after a restart, keep the newest identifier you stored and import it at the next start (9.5 step 1).

### 9.7 Error conditions

| Function | Return | Condition | What to do |
|---|---|---|---|
| uuid7_gen() | -1 | `out_buf` is NULL | fix the caller |
| uuid7_gen() | -2 | the RNG failed; nothing written, no sequence consumed | retry later; check the entropy source |
| uuid7_gen() | -3 | the next pair would be more than `UUID7_MAX_AHEAD_MS` ahead of the clock, or beyond 48 bits | back off until the wall clock advances; check the clock and the imported floor |
| uuid7_init(), uuid7_raise_floor() | -1 | `last_uuid7` is NULL (uuid7_raise_floor only) | fix the caller |
| uuid7_init(), uuid7_raise_floor() | -2, -3 | the imported value is not a version-7, variant-0b10 identifier | the stored key is not one of ours: fix the store |
| uuid7_init(), uuid7_raise_floor() | -4 | the imported identifier is dated beyond now + `UUID7_MAX_AHEAD_MS` | a poisoned key or a wrong clock: fix it before generating |

### 9.8 Recover runs

After -3 or a clock problem: correct the clock, restart, import the newest stored identifier (9.5). After -4 at start: find out why a stored key is dated in the future; do not start generating until it is resolved, since importing it would date every new key after it.

### 9.9 Secure installation and operation

Install only packages whose SHA-256 matches the pin of your `deps.txt` (`utils/deps.sh` refuses others). The built-in RNG is the kernel CSPRNG; a replacement callback must be as strong if your identifiers need to be unguessable. Treat identifiers as public.

## 10 Reference manual

### 10.1 Introduction

Quick reference: `uuid7_gen(buf)` → 16 bytes, 0 or -1/-2/-3; `uuid7_init(rng, last)`, `uuid7_raise_floor(last)` → 0 or -1/-2/-3/-4; `uuid7_set_rng_func(rng)` → 0; `uuid7_clock_failure_count()` → count.

### 10.2 Help method

The header's doc comments (this site's API reference, "Files" and "Topics") document every function, parameter and return value.

### 10.3 Screen definitions and operations

None: no screen.

### 10.4 Commands and operations

<!-- ecss:api -->

### 10.5 Error messages

The library prints nothing; its errors are the return values of <9.7>.

## 11 Tutorial {#sum_tutorial}

### 11.1 Introduction

Each scenario below is a test of `tests/plantuml/scenarios.c`: it runs against the library, asserts what it shows, and records the interaction as a sequence diagram. They are the training material of the library (ECSS-E-ST-40 5.7.2.2a).

### 11.2 Getting started

The library in one picture:

<!-- ecss:diagram name=class_uuid7 -->

### 11.3 Using the software on a typical task

<!-- ecss:scenarios -->

## 12 Analytical index

Not required: this manual is under 40 pages; the site's index and search cover it.
