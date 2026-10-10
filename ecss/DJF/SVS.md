# Software validation specification with respect to the TS (SVS) {#UUID7_DOC_SVS}

<!-- ecss:docinfo id=UUID7-DOC-SVS -->

## 1 Introduction

This SVS specifies the validation of uuid7 against its technical specification: the test designs, every test case with its inputs, outputs and criteria, the procedures that run them, and the analyses, inspections and reviews that validate what a test cannot reach (ECSS-E-ST-40 5.6.3.1). It is issued for the CDR. The test cases are generated from the tests themselves (their doc comments and `@verifies` tags) and from `ecss/DJF/verification_cases.csv`, so the specification and the tests cannot differ; the result of the latest run is shown with each case.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Software overview

The library under test, its configuration (test builds with `-DUUID7_TESTING`; release builds for the timing and binary checks), its environment and its interface: \ref UUID7_DOC_SRS "SRS" <4>, \ref UUID7_DOC_ICD "ICD".

## 5 Software validation specification task identification

### 5.1 Task and criteria

The tasks V1 to V5 of the \ref UUID7_DOC_SVALP "SValP" <5>; the items are the six profiles' archives and shared objects, the release artifacts and the two packages. A requirement is validated when every case linked to it passes.

### 5.2 Features to be tested

Every requirement of the TS with method T; the cases of <7> name them.

### 5.3 Features not to be tested

The statistical quality of the kernel's entropy (the kernel's); the clock-failure path and the entropy fallback by test (see <5.5>).

### 5.4 Test pass - fail criteria

The criterion of each case (<7>); a C test passes when every assertion holds and its executable exits 0 in every cell; a pipeline case passes when its stage reports ok.

### 5.5 Items that cannot be validated by test

| Item | Why not by test | Validated by |
|---|---|---|
| Clock failure behaviour (UUID7-SRS-026) | the test seams replace the clock read, so `clock_gettime()` failure cannot be injected without changing the code | inspection UUID7-IN-002 |
| Entropy fallback and zero fill (UUID7-SRS-019, -025, -070, -080) | `getrandom(2)` and `/dev/urandom` failures cannot be provoked from a test without privileges or interposition | inspection UUID7-IN-001 |
| Static and binary properties (UUID7-SRS-050 to -052, -060, -062 to -064, -090, -091, -121) | properties of the artifacts, not of a run | analyses UUID7-AN-001 to -010 |
| Burst capacity and non-blocking (UUID7-SRS-030, -041, -022) | a property of every execution, not observable in one | analyses UUID7-AN-011, -012 |
| Interface shape (UUID7-ICD-010, UUID7-SRS-071), initial state (UUID7-ICD-011), callback contract (UUID7-ICD-003), configuration (UUID7-SRS-122), error reporting (UUID7-SRS-100), static assertions (UUID7-SRS-081), coding standard (UUID7-SRS-061) | properties of the code's text | inspections and reviews UUID7-IN-003 to -008, UUID7-RV-001, -002 |

### 5.6 Manually and automatically generated code

All code is written by hand.

## 6 Software validation testing specification design

### 6.1 General

The cases are grouped by test design, each a suite or a pipeline stage.

### 6.2 Organization of each identified test design

#### 6.2.1 General

| Design | Grouping | Cases |
|---|---|---|
| TD-IT integration | function: one case per behaviour of the TS | `UUID7-TC-IT-*` |
| TD-SD scenarios | function: one end-to-end scenario per case, recorded as a sequence diagram | `UUID7-TC-SD-*` |
| TD-ST performance | the stress benchmarks, every profile x linkage | `UUID7-TC-ST-stress` |
| TD-PL pipeline gates | the ABI, hardening and packaging stages | `UUID7-TC-PL-*` |
| TD-PK installation | the installed-package smoke test | `UUID7-TC-PK-smoke` |

#### 6.2.2 Features to be tested

Per case in <7>, with the requirements traced.

#### 6.2.3 Approach refinements

Cases follow the requirements one to one where possible and add boundary cases (sequence exhaustion, the ahead bound, the 48-bit limit) and error cases (each return value). Results are analysed by comparison with expected values computed in the test (C cases), by the stage's verdict (pipeline cases), and by comparison with the budget (performance). The facility: <10>.

## 7 Software validation test case specification

### 7.1 General

Every test case, with its result in this refresh:

### 7.2 Organization of each identified test case

<!-- ecss:testcases -->

## 8 Software validation test procedures

### 8.1 General

Three procedures run every case: TP-1 the pipeline, TP-2 the analyses of `ecss/make.sh`, TP-3 the recorded inspections and reviews.

### 8.2 Organization of each identified test procedure

#### 8.2.1 Test procedure identifier

TP-1 pipeline, TP-2 analyses, TP-3 inspection and review.

#### 8.2.2 Purpose

- TP-1: run the cases of TD-IT, TD-SD, TD-ST, TD-PL and TD-PK (all `UUID7-TC-*`).
- TP-2: run the automated analyses (`UUID7-AN-*` and the automated `UUID7-IN-*`).
- TP-3: perform and record the manual inspections and reviews (`UUID7-IN-*`, `UUID7-RV-*` with a recorded verdict).

#### 8.2.3 Procedure steps

| Step | TP-1 | TP-2 | TP-3 |
|---|---|---|---|
| Log | `build/ecss/evidence/pipeline.txt`, `build/pipeline/<stage>.log`, `tests/results/logs/it/<cell>/<suite>.txt` | `build/ecss/evidence/<check>.txt` | the verdict, date and inspector in the case's record (rms result, or the notes of `verification_cases.csv`) |
| Set up | a checkout of the commit; `./utils/deps.sh sysroot` | TP-1 done | the source at the commit |
| Start | `ecss/make.sh` (or `./utils/run_pipeline.sh`) | `ecss/make.sh` (after TP-1) | open the item named by the case |
| Proceed | the stages in order; the suites in every cell | the checks of `ecss/tools/ecss.py` | apply the case's criteria |
| Result acquisition | the cmocka lines per test and cell, the stage summary | the verdict per check | the verdict |
| Shut down | Ctrl-C; nothing to clean | - | - |
| Restart | rerun from the start: the pipeline is all or nothing | rerun `ecss/make.sh --docs-only` | resume at the case |
| Wrap up | `build/ecss` holds every log | the site is regenerated | `load.sh` records the results in the rms |

#### 8.2.4 Test script

The scripts are the sources: `tests/ITs/integration_test.c`, `tests/plantuml/scenarios.c` with `puml_trace.h`, `tests/stress/*.c`, `tests/package/smoke.c`, `utils/run_pipeline.sh` (kyr-build `pipeline.sh`), `ecss/make.sh`, `ecss/tools/ecss.py`.

## 9 Software validation analysis, inspection, review of design

<!-- ecss:noncases -->

## 10 Validation test platform requirements

A Linux x86-64 host with the sysroot of `deps.txt` (the CPU family and C library of the target, so fully representative); pthreads; no simulator, no special equipment; the timing cases on an otherwise idle machine; for releases, the pinned build root.

## 11 Software validation specification additional information

1. and 2. Test/analysis/inspection/review to requirement, and requirement to case: the matrices below and \ref UUID7_DOC_TRACE "the traceability matrices".

<!-- ecss:case-matrix -->

3. and 4. Procedures to cases and cases to procedures: TP-1 runs every `UUID7-TC-*`; TP-2 every case whose record carries a `check`; TP-3 every case whose record carries a recorded verdict (some cases carry both: their verdict needs both).

5. and 6. Test scripts and detailed procedures: <8.2.3> and <8.2.4>.
