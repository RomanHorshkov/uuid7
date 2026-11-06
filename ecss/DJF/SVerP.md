# Software verification plan (SVerP) {#UUID7_DOC_SVERP}

<!-- ecss:docinfo id=UUID7-DOC-SVERP -->

## 1 Introduction

This SVerP describes how every product of the uuid7 life cycle is verified: which activities, on which inputs, producing which outputs, with which tools (ECSS-E-ST-40 5.8.2.1). It is issued for SRR and PDR. Verification answers "was the product built right": each requirement, design element and line of code is checked against what it derives from; validation (\ref UUID7_DOC_SVALP "SValP") checks the product against the TS.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Software verification process overview

### 4.1 General

Verification is mostly automatic: the pipeline's gates on every commit, and `ecss/make.sh`'s analyses and generated matrices at every refresh; the rest is inspection and review at the milestones. The effort is sized for category D (ECSS-Q-ST-80 5.3.1, 6.2.2 and 6.2.6.13 checked: no critical software, no independent verification).

### 4.2 Organization

1. Roles: the maintainer verifies; the pipeline applies the gates.
2. Reporting channel: the pipeline output and the \ref UUID7_DOC_SVR "SVR" of each refresh; findings become rms change-requests.
3. Authority for resolving problems: the maintainer.
4. Relationships: verification feeds the reviews (\ref UUID7_DOC_SREVP "SRevP") and the SPAMR.
5. Independence: none required at category D; the gates are machine-applied, the same for every change.

### 4.3 Master schedule

The SDP's <4.2>. Verification runs on every commit (gates) and on the commit of every review (the full package).

### 4.4 Resource summary

The maintainer; the workstation and the pinned build root; the tools of <4.7>.

### 4.5 Responsibilities

The maintainer: run the pipeline before every push, run `ecss/make.sh` before every review, perform and record the inspections, disposition every finding.

### 4.6 Identification of risks and level of independence

The SDP's <4.5>; independence: none (category D).

### 4.7 Tools, techniques and methods

| Technique | Tool |
|---|---|
| Static analysis | the catalog's warning set in six profiles; gcc `-fanalyzer` (audit); clang-format 18 |
| Dynamic analysis | ASan, UBSan, LSan (sanitize cells); the stress benchmarks |
| Interface checking | kyr-build's ABI gate (`abi/layout.txt`, exports) |
| Binary checks | `check_hardening.sh`, `nm`, `readelf`, `size` |
| Traceability analysis | `ecss/tools/ecss.py`: the `@satisfies` and `@verifies` tags against the rms links |
| Coverage | gcov through gcovr (coverage profile, -O0) |
| Inspection and review | the checklists of \ref sverp_checklists "Annex A"; the rms for records |

## 5 Control procedures for verification process

1. Problem reporting and resolution: an rms change-request per finding (\ref UUID7_DOC_SMP "SMP" <14>).
2. Deviation and waiver policy: a deviation is recorded as an rms decision linked to what it affects and reviewed at the next milestone. One deviation stands: the accessor name `uuid7_clock_failure_count()` against coding rule N3, kept to preserve the ABI (\ref UUID7_DOC_SPAMR "SPAMR" <6>); it has no record yet (\ref UUID7_DOC_SDP "SDP" <4.6>). No waiver.
3. Control procedures: the SCMP's change control.

## 6 Verification activities

### 6.1 General

One software item, the library; all its code is written by hand: there is no automatically generated code to verify separately.

### 6.2 Software process verification

| Process | Activity and how | Inputs | Outputs | Methods and tools |
|---|---|---|---|---|
| Requirements (TS) | each requirement checked for one obligation, a "shall", rationale, criteria, method, source; the rms `check` advisory findings read | requirements in the rms / CSV | review RIDs; \ref UUID7_DOC_TRACE "requirements traceability" (5.8.3.2a eo a) | inspection; kyr-rms-cli `check`, `gaps` |
| Architecture | every requirement allocated to a component; every component justified by a requirement | SDD, tags | allocation matrices (SDD <5.3>, 5.8.3.3a eo a) | traceability analysis (generated) |
| Design and code | the coding standard's gates; the analyses of the SVR | code | gate results; analyses | pipeline; ecss.py |
| Unit testing | the suites pass in every cell; every test traced | suites | \ref UUID7_DOC_UTR "unit test report" | CMocka, pipeline |
| Validation | the TS validated (SValP); the SVS traced to the TS | SVS, results | \ref UUID7_DOC_SVALR "validation report"; 5.8.3.8 traceability | pipeline, ecss.py |
| Delivery | packages reproducible, hardened, installable | debs | SVR checks; installation report | build_deb.sh twice, check_hardening.sh, package-smoke |
| Documentation | each document follows its DRD, regenerated from the commit | the site | review RIDs | review |

### 6.3 Software quality requirements verification (as per ECSS-Q-ST-80 clause 6.2.6.1)

#### 6.3.1 Activities

The quality requirements of the SRS <5.10> and the SPAP's metrics are verified by analysis at every refresh: coverage against the figures proposed in the \ref UUID7_DOC_SPAP "SPAP" <5.5>, header documentation (UUID7-SRS-091), warnings (UUID7-SRS-060), the technical budgets (UUID7-SRS-040, UUID7-SRS-050 to UUID7-SRS-054).

#### 6.3.2 Inputs

The coverage summary, doxygen's warning log of the header pre-pass, the build logs of the six profiles, the stress summary.

#### 6.3.3 Outputs

The verification cases' results in the \ref UUID7_DOC_SVR "SVR" and the SPAMR metrics.

#### 6.3.4 Methodology, tools and facilities

`ecss/tools/ecss.py` compares each measure with its target and records the verdict with its evidence file under `build/ecss/evidence`.

## Annex A Inspection and review checklists {#sverp_checklists}

The means of review and inspection (ECSS-Q-ST-80 6.2.6.9, 6.2.6.10); a finding against an item is a RID (review) or a failed inspection result (verification case), each naming the checklist and the item.

CL-DOC, a document of the data package against its DRD (reviews):

1. Every section of the DRD is present in its order; a section the tailoring empties says why, with the clause.
2. The document information names the document, its DRD, its reviews, its status and the configuration of the evidence; every configuration flag is explained.
3. Every compliance statement points to an output that exists; nothing claims more than the evidence and the front page's claim.
4. Generated parts are regenerated at the reviewed commit; nothing under `build/ecss/md` is edited by hand.
5. No contradiction with the other documents of the data package (deviations, open items, problem reports, statuses).
6. The standards are cited by clause, not copied.

CL-REQ, a requirement (SRR, PDR; UUID7-IN cases on requirements):

1. One obligation, one "shall", verifiable by the stated method.
2. Rationale, criteria, method and upper-level source present; no TBD or TBC; values with units.
3. Allocated to a component or stated as a property of the whole, and verified by at least one case.
4. Security relevance considered against the SSAR's assets.

CL-CODE, the code against the coding standard's rules that no gate enforces (UUID7-IN-008):

1. Each rule whose "Enforced by" column names the inspection is read against the code at the reviewed commit; a breach is a finding.
2. Every documented symbol carries its component (`@ingroup`) and the requirements it implements (`@satisfies`).
3. Failure paths leave the caller's outputs untouched; no new dependency beyond the C library.

CL-TEST, a verification case (CDR, QR):

1. It names the requirements it verifies (`@verifies` or the case's links) and its pass and fail criteria.
2. It is deterministic, or says where it uses the real clock or the entropy source.
3. It ran in every cell its plan names; its result links its evidence.
