# Software verification report (SVR) {#UUID7_DOC_SVR}

<!-- ecss:docinfo id=UUID7-DOC-SVR -->

## 1 Introduction

This SVR gathers the results of every verification activity the \ref UUID7_DOC_SVERP "SVerP" plans, for the commit it is generated from, organised by process as the DRD asks, with the budgets in <5> (ECSS-E-ST-40 5.8.3). Every result below was measured by `ecss/make.sh` on that commit; the evidence files are linked. Every verification here was performed by the developer; none was witnessed or repeated by anyone else. The package's claim:

<!-- ecss:claim -->

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Verification activities reporting and monitoring

### 4.1 General

All code is written by hand; there is no automatically generated code to report on separately.

### 4.2 Software related system requirements process verification (for the SRR)

Not applicable: no requirements baseline (\ref UUID7_DOC_TAILORING "tailoring"), and the RB verification report is N at category D (5.8.3.1). No system model.

### 4.3 Software requirements and architecture engineering process verification (for the PDR)

#### 4.3.1 Traceability

Software requirements to their upper-level sources: \ref UUID7_DOC_SRS "SRS" <7> and \ref UUID7_DOC_ICD "ICD" <7>. Architectural design to requirements: \ref UUID7_DOC_SDD "SDD" <5.3>. Both in one table: \ref UUID7_DOC_TRACE "the traceability matrices".

#### 4.3.2 Feasibility

At category D the requirements verification report (5.8.3.2a eo b), the architectural design verification report (5.8.3.3a eo b), the documentation verification report (5.8.3.10a) and the behaviour verification (5.8.3.13) are not required. Performed anyway, by generation: every requirement has a method and a source, every requirement is allocated to a component or is a property of the whole, every component implements a requirement:

<!-- ecss:gaps -->

### 4.4 Software design and implementation engineering process verification (for CDR)

1\. Traceability: the detailed design, code and unit test traceability matrices are not required at D (5.8.3.4a, 5.8.3.5a eo a, 5.8.3.6a); the code-level trace exists anyway, generated from the tags (\ref UUID7_DOC_TRACE "matrices"; in this site, the "Satisfies" and "Verifies" lists).

2\. Feasibility: the analyses performed on the code and the artifacts, with their results:

<!-- ecss:checks -->

Structural code coverage achievement (5.8.3.5b to d; the figures for category D are to be agreed with the customer; the \ref UUID7_DOC_SPAP "SPAP" <5.5> proposes them and no customer has agreed them, \ref UUID7_DOC_SDP "SDP" <4.6>, OI-02):

<!-- ecss:coverage -->

Deactivated and configurable code (ECSS-Q-ST-80 6.2.6.5, 6.2.6.6: N at D): the only configurable code is the test seams (`UUID7_TESTING`), absent from every shipped artifact (UUID7-AN-004), and the `__linux__` entropy branch, inspected (UUID7-IN-001). Robustness (5.8.3.5f: N at D): the audit profile's `-fanalyzer` and the sanitize cells run on every commit.

### 4.5 Software delivery and acceptance process verification (for QR and AR)

1. Traceability of acceptance tests to the RB: not applicable (no RB; acceptance is the consumers').
2. Coverage: <4.4>, updated at each refresh. Unit and integration testing coverage ratio: every requirement of method T has a test case (<4.6>).

### 4.6 Software validation process verification (for CDR, QR, AR)

1\. Traceability: validation specification to the TS (5.8.3.8a eo b):

<!-- ecss:req-results -->

The validation specification to the RB (5.8.3.8a eo a): not applicable.

The scenario runs, each recorded as a sequence diagram in the SVS:

<!-- ecss:scenario-results -->

2\. Feasibility: the validation report w.r.t. the TS is evaluated in the \ref UUID7_DOC_SVALR "SValR"; the requirements not covered by testing (ECSS-Q-ST-80 7.2.3.6) are verified by the analyses, inspections and reviews of the \ref UUID7_DOC_SVS "SVS" <9>, whose results are above.

### 4.7 Software quality requirements verification

The quality requirements (SRS <5.10> and the SPAP's metrics):

<!-- ecss:metrics -->

## 5 Margin and technical budget status

### 5.1 Technical budgets and margins computation

Memory: the sizes of the release object (`size`, `nm -S`): code, initialised and static data; the stack per call from `gcc -fstack-usage` on the release build, followed through the references between the functions (no recursion, no variable-length array). Processing: the mean cost per identifier of the single-thread benchmark (10 runs of 1,000,000 identifiers after a warm-up) in every profile and linkage; the margin is the budget of UUID7-SRS-040 minus the release measure. Each budget is a requirement (UUID7-SRS-040, UUID7-SRS-050, UUID7-SRS-051, UUID7-SRS-053, UUID7-SRS-054) with its verification case. The budgets are measured, not estimated: the code exists.

### 5.2 Software budget (sizing and timing)

<!-- ecss:budgets -->

### 5.3 Schedulability simulation and analyses

Not applicable: no task of its own, and the schedulability analysis is N at category D (5.8.3.11).

## 6 Numerical accuracy analysis

No floating point in the library. All arithmetic is on `uint64_t`: the timestamp (at most 2^48 - 1 after the checks) shifted by 12 bits fits 60 bits; `now + UUID7_MAX_AHEAD_MS` cannot overflow for any wall clock below 2^63 ms; milliseconds come from `tv_sec * 1000 + tv_nsec / 1000000` (truncation is the specified rounding, UUID7-SRS-006).

## 7 Interface timing analysis

The library controls one external interface, its function calls: the throughput and latency of uuid7_gen() are the benchmark figures of <5.2>; the other functions run in constant time with no system call except uuid7_init()/uuid7_raise_floor()'s clock read.
