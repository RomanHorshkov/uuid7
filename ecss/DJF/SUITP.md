# Software unit test plan (SUITP) {#UUID7_DOC_SUITP}

<!-- ecss:docinfo id=UUID7-DOC-SUITP -->

## 1 Introduction

This SUITP is the software unit test plan of uuid7 (ECSS-E-ST-40 5.5.2.9a). The software integration test plan is not produced: the library is one unit with nothing to integrate, and Annex R marks it N at category D. For the unit test plan Annex R applies "Y except Annex K sections 9, 10 and 11": sections <9> to <11> are omitted; the test cases and procedures are specified in the \ref UUID7_DOC_SVS "SVS".

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Software overview

The library, its configuration and environment are described in the \ref UUID7_DOC_SRS "SRS" <4>; its interface in the \ref UUID7_DOC_ICD "ICD". The unit under test is `app/uuid7.c` behind `app/uuid7.h`, built with the test seams (`-DUUID7_TESTING`) for the suites.

## 5 Software unit testing and software integration testing

### 5.1 Organization

The maintainer designs, writes and checks the tests; the pipeline runs them; findings become rms change-requests; configuration management fixes the commit tested; product assurance reads the results in the SPAMR.

### 5.2 Master schedule

The SDP's <4.2>. The tests run on every commit and on every review's commit; a run takes under a minute.

### 5.3 Resource summary

The maintainer; a Linux x86-64 machine; gcc, CMocka, gcovr from `deps.txt`.

### 5.4 Responsibilities

Managing, designing, preparing and executing the tests: the maintainer; execution also by the CI runner.

### 5.5 Tools, techniques and methods

CMocka; kyr-build's test steps (`lib/test.sh`: one executable per suite, linked against each profile's archive and shared object; a coverage build at -O0 with gcov); the sanitizers (sanitize profile); deterministic clock and RNG through the test seams; scripted, counting and failing RNG callbacks; threads (pthreads) for concurrency.

### 5.6 Personnel and personnel training requirements

C, CMocka, the platform's tooling; none to acquire.

### 5.7 Risks and contingencies

The SValP's <4.8>.

## 6 Control procedures for software unit testing / integration testing

1. Problem reporting and resolution: the SMP's.
2. Deviation and waiver policy: the SVerP's <5>.
3. Control procedures: a test is changed with the code it tests, in the same commit, through the same pipeline.

## 7 Software unit testing and integration testing approach

### 7.1 Unit/integration testing strategy

Black-box through the public interface plus the test seams, in every build profile and both linkages, so that what ships is what is tested; then once instrumented for coverage. Two suites: the integration suite (`tests/ITs`, requirement by requirement) and the scenario suite (`tests/plantuml`, behaviour end to end, each recorded as a sequence diagram).

### 7.2 Tasks and items under test

Items: `libuuid7.a` and `libuuid7.so` of the profiles debug, audit, sanitize, release, native and extreme, built with the test seams; criteria: every case passes in every cell.

### 7.3 Features to be tested

Every requirement of the TS whose method is test (\ref UUID7_DOC_SRS "SRS", \ref UUID7_DOC_ICD "ICD"): layout, ordering, clock handling, bounds, floor import, RNG selection and failure, concurrency, argument checks, constants.

### 7.4 Features not to be tested

The clock-failure path and the entropy fallback (unreachable through the seams; verified by inspection, UUID7-IN-001, UUID7-IN-002); the statistical quality of the kernel's entropy (the kernel's); exhaustion of the 48-bit field in real time (tested with the deterministic clock).

### 7.5 Test pass - fail criteria

A case passes when every assertion holds and its executable exits 0; a cell passes when all its cases pass; the suite passes when every cell passes.

### 7.6 Manually and automatically generated code

All code is written by hand; no separate activity.

## 8 Software unit test / integration test design

### 8.1 General

Two test designs, one per suite.

### 8.2 Organization of each identified test design

#### 8.2.1 Test design identifier

- TD-IT: the integration suite, `tests/ITs/integration_test.c`: one case per behaviour, grouped by layout, initialisation and import, errors, monotonicity and overflow, concurrency.
- TD-SD: the scenario suite, `tests/plantuml/scenarios.c`: one case per scenario a reader needs to understand the library.

#### 8.2.2 Features to be tested

Each case names the requirements it verifies (`@verifies`); the traceability is generated: \ref UUID7_DOC_TRACE "matrices".

#### 8.2.3 Approach refinements

Cases are selected per requirement and per error path (each return value of the ICD has a case), plus boundary cases (sequence 4095, the ahead bound, the 48-bit limit). Every case starts from a reset generator. Results are analysed by assertion (comparison with expected values computed in the test). Facility: the build host, the sysroot of `deps.txt`.

#### 8.2.4 Test case identifier

<!-- ecss:case-matrix -->
