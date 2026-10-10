# Interface control document (ICD) {#UUID7_DOC_ICD}

<!-- ecss:docinfo id=UUID7-DOC-ICD -->

## 1 Introduction

This ICD is the second part of the technical specification of uuid7: it specifies and designs the library's only external interface, the C header `app/uuid7.h` and the linkage of `libuuid7`, which every consumer compiles and links against. It is prepared for the PDR and updated for the CDR (ECSS-E-ST-40 5.4.3.5, 5.5.2.2).

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Software overview

See the \ref UUID7_DOC_SRS "SRS" <4>.

## 5 Requirements and design

### 5.1 General provisions to the requirements in the IRD

There is no IRD (\ref UUID7_DOC_TAILORING "the tailoring"). Each interface requirement has a unique identifier `UUID7-ICD-nnn` and an upper-level source, traced in <7>. Statuses and rms criticality read as in the SRS <5.1>.

### 5.2 Interface requirements

The interfaces are software-to-software only: no hardware interface, no man-machine interface, no protocol, no telecommand or telemetry. Behaviour on error: every function reports through its return value (UUID7-SRS-100). Timing: no call blocks (UUID7-SRS-030).

<!-- ecss:reqs doc=ICD section=5.2 level=4 -->

### 5.3 Interface design

The interface is the header `app/uuid7.h`, installed as `/usr/local/include/uuid7.h` by `libuuid7-dev`, and the shared object `libuuid7.so.<major>` (or the archive `libuuid7.a`). Its public ABI is committed in `abi/layout.txt` and gated by the pipeline (UUID7-ICD-009). Every item, from the header:

<!-- ecss:api -->

Data items:

| Item | Type, dimension | Range | Initial value | Unit |
|---|---|---|---|---|
| identifier (`out_buf`, `last_gen_uuid7`, `last_uuid7`) | `uint8_t[16]`, caller-owned | layout of UUID7-SRS-001 to UUID7-SRS-005; version 7, variant 0b10 | written by uuid7_gen() | - |
| `UUID7_SIZE_BYTES` | unsigned integer constant | 16 | 16 | bytes |
| `UUID7_MAX_AHEAD_MS` | unsigned integer constant | any value whose sum with the clock fits 64 bits | 10000 | ms |
| return values | `int` | 0, -1, -2, -3, -4 as each function states | - | - |
| clock failure count | `uint64_t` | 0 to 2^64 - 1, never decreasing | 0 | failures |
| RNG callback | `uuid7_rng_function_t` | any function filling `n` bytes, or NULL | NULL (built-in on first use) | - |

The class diagram of the interface ({{fig:class_uuid7}}):

<!-- ecss:diagram name=class_uuid7 -->

## 6 Validation requirements

<!-- ecss:validation-matrix doc=ICD -->

## 7 Traceability

The validation traceability (interface requirement to test) is in the DJF: \ref UUID7_DOC_TRACE "the matrices".

<!-- ecss:upward-trace doc=ICD -->
