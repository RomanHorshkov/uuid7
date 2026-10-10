# Software design document (SDD) {#UUID7_DOC_SDD}

<!-- ecss:docinfo id=UUID7-DOC-SDD -->

## 1 Introduction

This SDD describes the architectural and the detailed design of uuid7 and its internal interfaces (ECSS-E-ST-40 5.4.3, 5.5.2). It is issued for the PDR (architecture) and the CDR (detailed design). At category D the detailed design may omit what the code already states (Annex R: Ytba); the code's doc comments complete it, and the component descriptions below are generated from them, so they cannot drift.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Software design overview

### 4.1 Software static architecture

One translation unit, `app/uuid7.c`, behind one header, `app/uuid7.h`, built into a static archive and a versioned shared object. Inside, seven components (doxygen groups) around one shared datum, the reservation state ({{fig:component_uuid7}}):

<!-- ecss:diagram name=component_uuid7 -->

States and modes: none. The library is always in its single operating state; the only mode-like distinction is the build: with `-DUUID7_TESTING` the test seams exist (never in a shipped artifact).

Mission and configuration data: there is no mission data. The one configuration datum is the compile-time bound `UUID7_MAX_AHEAD_MS` (reference data of the product family, default 10000 ms); the RNG callback is run-time configuration chosen by the caller.

### 4.2 Software dynamic architecture

The library has no thread, no timer and no interrupt of its own: it is passive, executed on the callers' threads. The design choice that copes with concurrency is a lock-free reservation: one 64-bit atomic word updated by compare-and-swap, so a call never blocks and some caller always progresses. There is no real-time deadline of the library's own; the cost per call is budgeted (UUID7-SRS-040).

### 4.3 Software behaviour

The behaviour of one generation is the reservation rule ({{fig:reservation_state}}):

<!-- ecss:diagram name=reservation_state -->

(The software behaviour model of 5.4.3.4a is not required at category D; {{fig:reservation_state}} documents the rule the requirements of the SRS <5.2> specify.)

### 4.4 Interfaces context

The only external interface is the C interface to the caller (\ref UUID7_DOC_ICD "ICD"); below it, the library calls the C library for the clock (`clock_gettime`), the entropy (`getrandom`, `open`, `read`, `close`) and `memset`. The package view of the library and what it depends on ({{fig:package_uuid7}}):

<!-- ecss:diagram name=package_uuid7 -->

The exported symbols are held to the header by the ABI gate ({{fig:abi_exports}}):

<!-- ecss:diagram name=abi_exports -->

### 4.5 Long lifetime software

The library depends on ISO C11 and four POSIX calls; the only Linux-specific call (`getrandom(2)`) has a portable fallback (`/dev/urandom`). The 48-bit millisecond field lasts until the year 10889; the generator refuses rather than wraps (UUID7-SRS-012).

### 4.6 Memory and PU budget

<!-- ecss:budgets -->

### 4.7 Design standards, conventions and procedures

1. Architectural design method: functional decomposition into components, each a doxygen group whose members are tagged `@ingroup`; the component diagram is generated from the tags and from the call references doxygen extracts.
2. Detailed design method: design by contract in the doc comments (preconditions, return values, effects on the state), plus the state diagram above.
3. Code documentation standard: doxygen doc comments, rules D1 to D4 of the \ref UUID7_DOC_CODING_STANDARD "coding standard".
4. Naming conventions: rules N1 to N6 of the coding standard (NAMING.md).
5. Programming standards: the coding standard.
6. Intended reuse components: the whole library (\ref UUID7_DOC_SRF "SRF").
7. Main design trade-offs, each summarised here with its full record (alternatives, consequences, rationale, the requirements it answers, what justifies it) in the DTJ <4> (ECSS-E-ST-40 4.2.4, 4.2.5):
<!-- ecss:tradeoffs-summary -->
   - one atomic word instead of a lock: no blocking, but sequence and time share 64 bits (52 bits of milliseconds, 12 of sequence);
   - borrowing future milliseconds instead of waiting: non-blocking, bounded by `UUID7_MAX_AHEAD_MS`;
   - entropy before reservation: a failed RNG wastes nothing, at the cost of drawing entropy for calls that later refuse;
   - binary-only interface: smaller, but the caller formats.
<!-- ecss:end -->

## 5 Software design

### 5.1 General

The architecture of <4.1>: the components, their hierarchy (all children of the library), their dependencies (the arrows of the component diagram) and their interfaces (<5.4>, <5.5>). No flight software, no in-flight modification. The structure of <5.2> to <5.5> follows.

### 5.2 Overall architecture

Static view: <4.1> ({{fig:component_uuid7}}); the public interface as a class diagram ({{fig:class_uuid7}}):

<!-- ecss:diagram name=class_uuid7 -->

Dynamic view: the computational model is the caller's: any number of threads, pre-emptive, no priorities of the library's own. Components taking part in the real-time behaviour: the reservation state (a protected object without a lock: compare-and-swap). Communication and synchronisation: the reservation word (acquire-release compare-and-swap), the RNG pointer (release store, acquire load), the clock-failure counter (relaxed increments). Time management: the wall clock read once per reservation attempt. Concurrent generation, recorded by a scenario test ({{fig:sd_concurrent_generation}}):

<!-- ecss:diagram name=sd_concurrent_generation -->

Behaviour: the state diagram of <4.3> ({{fig:reservation_state}}); one generation as the scenario test records it ({{fig:sd_generate_one}}):

<!-- ecss:diagram name=sd_generate_one -->

Error handling and fault tolerance: errors are detected at the boundary (NULL arguments, invalid imported identifiers, bounds) and reported by the return value before any state changes; the RNG is called before the reservation so its failure consumes nothing ({{fig:sd_entropy_failure}}); a clock that stalls or steps back leaves the order to the state ({{fig:sd_clock_backwards}}); a failed clock read is counted and survived (timestamp 0, ordering kept by the state); the built-in RNG zeroes what it could not fill; a poisoned floor and the ahead bound are refused before the state moves ({{fig:sd_abuse_bounds}}). Fault containment: the library writes only into the caller's buffer and its own static state.

<!-- ecss:diagram name=sd_entropy_failure -->

<!-- ecss:diagram name=sd_clock_backwards -->

<!-- ecss:diagram name=sd_abuse_bounds -->

The remaining behaviours the scenario tests record: the sequence within one millisecond ({{fig:sd_same_millisecond}}), the floor imported after a restart ({{fig:sd_restart_with_floor}}) and the replaceable RNG ({{fig:sd_configuration}}).

<!-- ecss:diagram name=sd_same_millisecond -->

<!-- ecss:diagram name=sd_restart_with_floor -->

<!-- ecss:diagram name=sd_configuration -->

### 5.3 Software components design — General

The components, their purpose and development type (all new development, all category D), with the requirements each implements (from the code's tags):

<!-- ecss:allocation -->

The library is written for reuse: its provided functionality and external interfaces are the ICD's. It reuses no existing component other than the C library.

### 5.4 Software components design — Aspects of each component

Generated from the components' records and the code (members, signatures, calls between components).

<!-- ecss:components -->

### 5.5 Internal interface design

The components share three data items and call each other's functions:

| Interface | Between | Data element | Type, dimension | Initial value |
|---|---|---|---|---|
| reservation word | Generator, Floor import ↔ Reservation state | `g_v7_state`: bits 63..12 the millisecond, 11..0 the sequence | `_Atomic uint64_t` | 0 |
| RNG pointer | Generator, Floor import (uuid7_init) ↔ Entropy | `g_uuid_rng_func` | `_Atomic(uuid7_rng_function_t)` | NULL (built-in on first use) |
| clock reading | Generator, Floor import ↔ Clock | `_get_realtime_ms()` return | `uint64_t` ms | - |
| failure counter | Clock ↔ caller (uuid7_clock_failure_count) | `g_clock_failures` | `_Atomic uint64_t` | 0 |
| tail bytes | Generator ↔ Entropy (`_fill_random`) | `rb` | `uint8_t[8]` on the caller's stack | - |

The complete map of calls and references is the doxygen call and caller graphs of each function in this site.

## 6 Requirements to design components traceability

Forward and backward: the matrices of <5.3>; the complete chain requirement → component → code → test → result is in the DJF (\ref UUID7_DOC_TRACE "matrices"). Specific measures for critical software: none (category D).
