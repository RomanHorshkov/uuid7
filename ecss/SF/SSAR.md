# Software security analysis report (SSAR) and security risk treatment plan {#UUID7_DOC_SSAR}

<!-- ecss:docinfo id=UUID7-DOC-SSAR -->

## 1 Introduction

The security analysis of uuid7 and the treatment of its security risks (ECSS-E-ST-40 5.11.3, 5.11.4; ECSS-Q-ST-80 6.2.9). These outputs have no DRD; the sections follow the clauses. The methods and the review frequency are in the SSMP (\ref spap_security "SPAP <6.4>"). The analysis is reviewed at every milestone, on every change request that touches the entropy, clock or import code, and when a new threat or vulnerability becomes known (5.11.3k).

## 2 Basis of the analysis

uuid7 is not part of a system with a security analysis above it: it is at the top architectural level for its own analysis (5.11.3d NOTE), and its results feed its consumers' analyses (5.11.3e) through the \ref UUID7_DOC_SUM "SUM" <9.4> and <9.9>. Higher-level inputs: RFC 9562 6.9 (unguessability) and 6.12 (opacity), the platform's rule that every artifact is pinned by hash (DEPENDENCIES.md). No lower-level supplier (5.11.3g, h not applicable).

## 3 Assets and entry points

| Asset | Why it matters |
|---|---|
| A1 Ordering of identifiers | consumers append keys in order; a misordered key slows or breaks their stores |
| A2 Uniqueness of identifiers | a duplicate key is a refused or overwritten record |
| A3 Unpredictability of the tail | consumers that expose identifiers rely on them being hard to guess |
| A4 The released packages | a tampered package runs inside every consumer |

Entry points: the five functions (arguments, imported floors, the RNG callback), the wall clock, the entropy source, the build and the distribution of the packages.

## 4 Threats, vulnerabilities and risks

| Risk | Threat → vulnerability | Assets | Likelihood | Impact | Level |
|---|---|---|---|---|---|
| R1 Poisoned floor | a stored key dated far in the future (corruption, attack on the store) → importing it would date every later identifier after it | A1 | low | high | medium |
| R2 Runaway logical clock | sustained generation or abuse → borrowing future milliseconds without limit, wrapping the 48-bit field | A1, A2 | low | high | medium |
| R3 Entropy failure | the kernel source fails → identifiers with a predictable or zero tail | A2, A3 | very low | medium | low |
| R4 Weak replacement RNG | a caller installs a weak callback → guessable tails | A3 | caller's choice | medium | low |
| R5 Identifiers used as secrets | a consumer uses an identifier as an access token → its timestamp is public and its tail only 62 bits | A3 | possible | medium | medium |
| R6 Clock manipulation | the clock is set back or forward → misordering or refusals | A1 | low | low | low |
| R7 Descriptor leak | `/dev/urandom` descriptor inherited by a child process | A3 | very low | low | low |
| R8 Tampered package | a modified deb in transit or in the cache | A4 | low | high | medium |
| R9 Memory corruption | an overflow in the library | A1 to A4 | very low | high | low |

The mapping between risks and vulnerabilities is the "Threat → vulnerability" column (5.11.3i); it is checked whenever either changes (5.11.3j).

## 5 Sensitivity classification of the components

No component is security sensitive in the sense of ECSS-Q-ST-80 6.2.10: none holds a secret, none enforces access control. The Entropy component (UUID7-CMP-RNG) and the Floor import (UUID7-CMP-IMPORT) carry the security-relevant behaviour and are reviewed first at each milestone (5.11.3l).

## 6 Security risk treatment plan {#ssar_treatment}

Risk level accepted without treatment: low (5.11.4a NOTE 2); every medium risk is treated.

| Risk | Treatment | Requirements (5.11.4d) | Verification | Status |
|---|---|---|---|---|
| R1 | refuse a floor beyond now + UUID7_MAX_AHEAD_MS (-4), state untouched | UUID7-SRS-018 | UUID7-TC-IT-raise_floor_refuses_far_future, UUID7-TC-SD-abuse_bounds | implemented, verified |
| R2 | refuse a reservation beyond the bound or the 48-bit field (-3) | UUID7-SRS-011, UUID7-SRS-012 | UUID7-TC-IT-gen_refuses_beyond_ahead_bound, UUID7-TC-IT-gen_refuses_48bit_limit | implemented, verified |
| R3 | kernel CSPRNG with fallback; failure returns -2 and writes nothing; partial bytes zeroed | UUID7-SRS-019, UUID7-SRS-024, UUID7-SRS-025 | UUID7-IN-001, UUID7-TC-IT-rng_failure_consumes_no_sequence | implemented, verified |
| R4 | documented: a replacement must be as strong (accepted, the caller's choice) | - | SUM <9.9> | accepted |
| R5 | documented: identifiers are not secrets; no state disclosed beyond them | UUID7-SRS-071 | UUID7-RV-002; SUM <9.4> | transferred to the consumers' analyses (5.11.4b) |
| R6 | ordering kept whatever the clock does; bounded borrowing | UUID7-SRS-009, UUID7-SRS-011 | UUID7-TC-IT-time_regression_monotonic | implemented, verified |
| R7 | `O_CLOEXEC`, close on every path | UUID7-SRS-070 | UUID7-IN-001 | implemented, verified |
| R8 | SHA256SUMS with every release; consumers pin each deb by SHA-256; reproducible builds; hardened binaries | UUID7-SRS-121, UUID7-SRS-072 | UUID7-AN-008, UUID7-TC-PL-hardening | implemented, verified |
| R9 | no allocation, fixed-size buffers, sanitizers in every run, `-fanalyzer` | UUID7-SRS-051, UUID7-SRS-060 | UUID7-AN-001, UUID7-AN-005 | implemented, verified |

The verification status of each requirement is in the \ref UUID7_DOC_TRACE "matrices"; a treatment is monitored by its requirement's status at every refresh (5.11.4f). Residual vulnerabilities (5.11.5.3b): R4 and R5, both on the caller's side and documented.

## 7 Security in the life cycle

Requirements (SRS <5.8>) and design (SDD <5.2> error handling) carry the treatments; the tests and inspections above verify them in every refresh; the delivery carries the integrity values; maintenance re-runs this analysis on any change of the entropy, clock or import code. The library holds no data at rest: data sanitation (5.11.5.6g to i) has nothing to sanitise.
