# Software reuse file (SRF) {#UUID7_DOC_SRF}

<!-- ecss:docinfo id=UUID7-DOC-SRF -->

## 1 Introduction

uuid7 is software developed for intended reuse: every platform repository that needs time-ordered keys reuses it. This SRF documents it so that a project reusing it can decide on its reuse from this file alone (ECSS-E-ST-40 5.4.3.6, 5.4.3.7; ECSS-Q-ST-80 6.2.7). The library itself reuses no existing software other than the C library, assessed in <4> as well.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Presentation of the software intended to be reused

| Information | uuid7 | The C library (reused by uuid7) |
|---|---|---|
| 1. Name and main features | uuid7: RFC 9562 UUIDv7, monotonic, thread-safe, lock-free, floor import | glibc: clock_gettime, getrandom, open/read/close, memset |
| 2. Developer | Roman Horshkov | the GNU project, as packaged by Ubuntu |
| 3. Version and components | VERSION of this package; one component file app/uuid7.c behind app/uuid7.h | the one the evidence ran with (\ref UUID7_DOC_SCF "SCF", build environment) |
| 4. Licensing | MIT (LICENSE) | LGPL-2.1-or-later |
| 5. Industrial property, exportability | none known; no cryptographic function of its own (it consumes the kernel's entropy) | the distribution's |
| 6. Language | C11 | C |
| 7. Development and execution environment | Linux x86-64, gcc, kyr-build | Linux |
| 8. Warranty, maintenance, installation, training | no warranty (MIT); maintained per the \ref UUID7_DOC_SMP "SMP"; installed from its debs (\ref UUID7_DOC_SUM "SUM" <9.2>); training: SUM <11> | the distribution's security updates |
| 9. Commercial software needed | none | none |
| 10. Size | the metrics below | - |

<!-- ecss:metrics -->

## 5 Compatibility of existing software with project requirements

a. The part of a reusing project's requirements uuid7 implements: unique, time-ordered, 16-byte identifiers, generated concurrently, ordered across restarts (the \ref UUID7_DOC_SRS "SRS" <5.2>).

b. Availability and quality of the information, for uuid7:

| Information | Availability and status |
|---|---|
| 1. Requirements documentation | SRS and ICD, generated from the rms, every requirement verified (see the status below) |
| 2. Architectural and detailed design | SDD, generated from the code's components and doc comments |
| 3. Traceability system → software requirements | the upper-level sources per requirement (SRS <7>); no system requirements |
| 4. Traceability requirements → design → code | generated (`@satisfies`), \ref UUID7_DOC_TRACE "matrices" |
| 5. Unit tests documentation and coverage | SUITP, unit test report; coverage in the SVR |
| 6. Integration tests documentation and coverage | not applicable: one unit |
| 7. Validation documentation and coverage | SValP, SVS, validation report: every TS requirement |
| 8. Verification reports | SVR |
| 9. Performance | SVR <5>: memory and cost per identifier |
| 10. Operational performance | in use by the platform's libraries and products that pin it (kyr_lmdb, kyr_core, kyr_drive, kyr_estate, kyr_rms) |
| 11. Residual nonconformances and waivers | none (\ref UUID7_DOC_SPR "problem reports") |
| 12. User documentation | SUM |
| 13. Code quality | the coding standard's gates and analyses (SPAMR <6>) |

<!-- ecss:summary -->

c. Quality level: the whole package above is produced for category D; a project that reuses uuid7 at a higher category must add what its category requires (Annex R): independent verification, the unit test plan's sections 9 to 11, MC/DC coverage targets, the robustness and code verification reports.

## 6 Software reuse analysis conclusion

1. Decision: reuse uuid7 as is for keys of category D functions; the C library is reused as the platform's base.
2. Estimated level of reuse: 100 % (no modification; the bound `UUID7_MAX_AHEAD_MS` is the only adaptation).
3. Assumptions and method: the reusing project links the released packages unmodified and imports its newest stored identifier at start; the level is estimated by the absence of any required modification.

## 7 Detailed results of evaluation

The detailed results are the verification and validation reports of this package: \ref UUID7_DOC_SVR "SVR", \ref UUID7_DOC_SVALR "SValR", \ref UUID7_DOC_UTR "unit test report".

## 8 Corrective actions

None identified. Implemented: none.

## 9 Configuration status

The configuration item's identification, contents and integrity values: \ref UUID7_DOC_SCF "SCF"; its release: \ref UUID7_DOC_SRELD "SRelD".
