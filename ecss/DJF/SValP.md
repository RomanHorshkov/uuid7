# Software validation plan (SValP) {#UUID7_DOC_SVALP}

<!-- ecss:docinfo id=UUID7-DOC-SVALP -->

## 1 Introduction

This SValP describes how uuid7 is validated: that the library, as built and packaged, meets every requirement of its technical specification (ECSS-E-ST-40 5.6.2.1). It is issued for the PDR.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Software validation process planning

### 4.1 General

Validation is against the technical specification only: there is no requirements baseline (\ref UUID7_DOC_TAILORING "tailoring"), and the consumers validate the library within their systems. The effort is the full regression on every change, automated; independence is not required (category D). All code is written by hand: no separate activity for generated code. The quality requirements are validated like the others (UUID7-SRS-040, UUID7-SRS-050 to UUID7-SRS-054, UUID7-SRS-060, UUID7-SRS-091).

### 4.2 Organization

1. Structure: the maintainer designs, prepares, runs and checks the validation; the pipeline executes it.
2. Relationships: validation results feed the reviews and the rms (results recorded per case); configuration management fixes the commit validated; product assurance reads the SPAMR.
3. Independence: none (category D).

### 4.3 Schedule

The SDP's <4.2>. Validation runs on every commit (the pipeline) and is reported on the commit of every review (CDR: validation w.r.t. TS; QR: complete). Test readiness: the pipeline green on that commit, the rms export of that commit present. The test facilities are always available (the workstation, CI runners).

### 4.4 Resource summary

The maintainer; a Linux x86-64 machine with the sysroot of `deps.txt`; CMocka, gcovr; no simulator.

### 4.5 Responsibilities

Managing, designing, preparing, executing, witnessing and checking: the maintainer (and the CI runner for execution).

### 4.6 Tools, techniques and methods

The CMocka suites (integration and scenario), the stress benchmarks, the installed-package smoke test, the pipeline (kyr-build `pipeline.sh`), the analyses of `ecss/make.sh`. The validation facility is the target itself: the library runs natively on the machine it is built for; the environment is fully representative (same processor family, same C library); software in the loop; observability through the test seams (deterministic clock, forced RNG failure, state reset), which exist only in test builds; the timing measures run on non-instrumented release builds.

### 4.7 Personnel requirements

C and the platform's tooling; no training need.

### 4.8 Risks

The one risk to the campaign: a test that depends on timing. Contingency: the tests use the deterministic clock wherever the outcome depends on time, and the real clock only where the requirement is about it (UUID7-SRS-006), with bounds read around the call.

## 5 Software validation tasks identification

| Task | Items under test | Criteria | Inputs | Outputs |
|---|---|---|---|---|
| V1 Functional and interface validation | libuuid7 in six profiles, static and shared | every assertion holds in every cell | the suites | logs per cell; \ref UUID7_DOC_UTR "unit test report" |
| V2 Behaviour scenarios | libuuid7, test build | every scenario passes; its diagram is written | tests/plantuml | logs; .puml sequence diagrams |
| V3 Performance | release and other profiles, static and shared | release single-thread mean cost <= 100 ns | tests/stress | stress summary |
| V4 Installation | the two debs, extracted | the smoke test builds from them only and passes | tests/package/smoke.c | package-smoke log |
| V5 Analyses, inspections, reviews | release artifacts, sources | the criteria of each case | SVS <9> | evidence files |

When testing resumes after an interruption, every task is repeated: the pipeline is all or nothing. The detailed cases and procedures are the \ref UUID7_DOC_SVS "SVS".

## 6 Software validation approach

Every requirement of the TS has at least one case; the method of the requirement decides the kind: test (T) for behaviour and interface, analysis (A) for properties of the binary or of the code as a whole, inspection (I) for what a test cannot reach (the entropy fallback, the clock failure path), review (R) for properties of the interface's shape. Regression strategy: every change runs every task V1 to V5 (the pipeline and `ecss/make.sh`); in particular any change of tool, platform or build root reruns them all (ECSS-Q-ST-80 6.3.5.17, 6.3.5.18).

## 7 Software validation testing facilities

The facility of <4.6>: the pinned build root, in which `ecss/make.sh` runs the gates when it exists, else the workstation (then a host build, not the release environment); the environment and toolchain of each run are read where it ran and printed in the \ref UUID7_DOC_SCF "SCF"; the CI runners of the shared cicd workflow `c-quality.yml`. The test configuration of each run is identified by the commit and the sysroot of `deps.txt`. No RB validation, so one environment only.

## 8 Control procedures for software validation process

1. Problem reporting and resolution: the SMP's (rms change-requests).
2. Deviation and waiver policy: the SVerP's <5>.
3. Configuration control: the commit validated is the one reviewed and tagged; results are recorded in the rms against that commit (`load.sh`).

## 9 Complement of validation at system level

Requirements that cannot be validated in this environment: none of the TS. What the consumers validate in their own systems (their keys appending in order under their load) is theirs.
