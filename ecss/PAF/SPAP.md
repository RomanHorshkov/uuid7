# Software product assurance plan (SPAP) {#UUID7_DOC_SPAP}

<!-- ecss:docinfo id=UUID7-DOC-SPAP -->

## 1 Introduction

This SPAP states how product assurance is organised and performed for uuid7, a category D library, in response to ECSS-Q-ST-80 as \ref UUID7_DOC_TAILORING "tailored". It also carries the software security management plan (<6.4>), as ECSS-E-ST-40 5.11.2a NOTE 3 allows. It is issued for SRR and PDR and revisited at every milestone (ECSS-Q-ST-80 5.2.1.3); `ecss/make.sh` regenerates it with the product.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 System overview

The library and its context are described in the \ref UUID7_DOC_SRS "SRS" <4> and the \ref UUID7_DOC_SDD "SDD" <4>.

## 5 Software product assurance programme implementation

### 5.1 Organization

1. Structure: one maintainer, Roman Horshkov, who develops, verifies, assures and reviews (\ref UUID7_DOC_SDP "SDP" <4.7>). ECSS-Q-ST-80 5.1.4.2 (Y at D) asks for a PA function with organisational authority over development, which one person cannot be: a declared deviation (\ref UUID7_DOC_SDP "SDP" <4.6>, OI-14).
2. Interfaces: the consuming repositories (customers of the packages), the kyr-build maintainers (the same person) for the tooling.
3. System-level product assurance and safety: none above the library; the classification is in \ref UUID7_DOC_TAILORING "the tailoring" <3>.
4. Independence: not required at category D (ECSS-Q-ST-80 5.1.3.2: N). The pipeline's gates apply the same checks to every change, but they are the developer's own tools, not an independent check.
5. Delegation: none.

### 5.2 Responsibilities

The software product assurance function (the maintainer): keep the plans current; run `ecss/make.sh` before every review and release; read the SPAMR's metrics and gaps; disposition problem reports and nonconformances; refuse a release whose pipeline or package is not green.

### 5.3 Resources

The maintainer's time; the workstation and the pinned build root; the tools of the SDP <5.4>; the rms.

### 5.4 Reporting

Product assurance reporting is the \ref UUID7_DOC_SPAMR "SPAMR", regenerated at every refresh and frozen with each review's rms baseline. No audit is planned (category D: only when necessary, Table D-2 5.2.3); no alert has been issued or received (5.2.4).

### 5.5 Quality models

The quality model uses the characteristics of ECSS-Q-ST-80 5.2.7.2 that are relevant at category D:

| Characteristic | Quality requirement | Metric and target |
|---|---|---|
| Functionality | every TS requirement verified | requirements with a passing verification / all: 100 % |
| Reliability | no failure in validation | failed cases: 0 |
| Maintainability | documented interface, coding standard | header doxygen warnings: 0 (UUID7-SRS-091); format stage ok |
| Reusability | self-contained, platform-agnostic | UUID7-SRS-063 verified |
| Security | entropy and bounds | the SSAR's treatments verified |
| Efficiency | generation cost | <= 100 ns per identifier, release (UUID7-SRS-040) |
| Portability | C11, POSIX fallback | UUID7-SRS-080, UUID7-SRS-081 verified |
| Development effectiveness | coverage | statement >= 80 %, decision >= 65 % (UUID7-SRS-090) |

Suitability for safety is not relevant at category D; usability has no user interface to measure.

### 5.6 Risk management

Product assurance contributes the risks it finds (failed gates, coverage drops, suspect links in the rms) to the SDP <4.5> list; there is no project risk policy above (ECSS-M-ST-80 not applied).

### 5.7 Supplier selection and control

No supplier.

### 5.8 Methods and tools

The methods and tools of the \ref UUID7_DOC_SDP "SDP" <5.3> and <5.4>, identified and justified below (ECSS-Q-ST-80 5.6.1.1, 5.6.1.2): the version each one had in this refresh, its use, its prior use (the category D condition of 5.6.1.1: used successfully in at least one project before), how its output is checked (5.6.1.3) and its availability. Two do not meet the prior-use condition: `ecss/tools/ecss.py`, written for this package, and kyr_rms as the source of truth of an ECSS package; their outputs are cross-checked instead (drift, refusal, review). No customer has agreed the list (5.6.1.1a; \ref UUID7_DOC_SDP "SDP" <4.6>).

<!-- ecss:tools anchors=yes -->

### 5.9 Process assessment and improvement

Not applicable at category D (ECSS-Q-ST-80 5.7: N). Improvements are made through the shared tooling (kyr-build), which every repository pins.

### 5.10 Operations and maintenance

The quality measures of maintenance are those of development: every modification goes through the same pipeline and the same regenerated documents (\ref UUID7_DOC_SMP "SMP" <12>).

## 6 Software process assurance

### 6.1 Software development cycle

The life cycle is the SDP's <5.2>. The milestone immediately before validation is the test readiness check of every review: the pipeline green on the commit under review (ECSS-Q-ST-80 6.1.5 asks for a TRR before validation against the RB; there is no RB, and the TS validation's readiness is that check).

### 6.2 Projects plans

| Plan | Document | Prepared | Updated |
|---|---|---|---|
| Development | \ref UUID7_DOC_SDP "SDP" | SRR | every milestone |
| Reviews | \ref UUID7_DOC_SREVP "SRevP" | SRR | every milestone |
| Configuration management | \ref UUID7_DOC_SCMP "SCMP" | SRR | when the practice changes |
| Product assurance and security | this SPAP | SRR | every milestone |
| Verification | \ref UUID7_DOC_SVERP "SVerP" | SRR | PDR |
| Validation | \ref UUID7_DOC_SVALP "SValP" | PDR | CDR |
| Unit tests | \ref UUID7_DOC_SUITP "SUITP" | CDR | every change of the suites |
| Maintenance | \ref UUID7_DOC_SMP "SMP" | QR | every release |

Every plan is regenerated by `ecss/make.sh` and is current for the commit it is generated from.

### 6.3 Software dependability and safety

Category D: no critical software, no critical item (ECSS-Q-ST-80 5.3.2), no specific measure (6.2.3 N at D). The engineering measures that keep failures contained are design rules anyway: no allocation, no lock, no I/O beyond the entropy source, outputs untouched on failure (\ref UUID7_DOC_CODING_STANDARD "coding standard" E3, M1).

### 6.4 Software security {#spap_security}

This section is the software security management plan (ECSS-E-ST-40 5.11.2a, NOTE 3).

1. Objectives: identifiers that cannot be forged into the future, an entropy source that fails loudly, no state disclosed beyond the identifiers, packages whose integrity consumers verify.
2. Organisation and roles: the maintainer is the security manager (5.11.2c: role shared, category D).
3. Relation to supplier processes: none beyond the maintainer's; the platform's security reviews of its consumers take this plan's outputs as input.
4. Stakeholders: the consumers, which must not use identifiers as secrets (\ref UUID7_DOC_SUM "SUM" <9.9>).
5. Planning: the security analysis is reviewed at every review (SRevP <6>) and on every change of the entropy or import code.
6. Activities and outputs: the \ref UUID7_DOC_SSAR "SSAR" with its risk treatment plan; the security requirements of the SRS <5.8>; their verification in the SVS.
7. Tools and methods: threat analysis of the interface (assets, entry points, misuse), code inspection, the sanitizers and the hardening gate.
8. Review frequency: every milestone, and when RFC 9562 errata or a CVE of the C library's entropy path appear.

Security assurance (ECSS-Q-ST-80 6.2.9.1): the security requirements and their verification are traced like the others; the SSAR finds no security sensitive component, so ECSS-Q-ST-80 6.2.10.1 and 6.2.10.2 are not applied; 6.2.10.3 and 6.2.10.4 are met by the full regression on any tool or platform change. Integrity and authenticity of the product (6.2.4.8, 6.2.4.9): SHA256SUMS published with each release, each deb pinned by SHA-256 in its consumers' `deps.txt`, the build root's identity in each `.buildinfo`.

### 6.5 Software documentation and configuration management {#spap_ncr}

Documentation and configuration management follow the \ref UUID7_DOC_SCMP "SCMP"; product assurance checks at each review that the package was regenerated from the reviewed commit (the docinfo of every document names it). Nonconformance control: a software problem is an rms change-request (\ref UUID7_DOC_SMP "SMP" <14>); a problem found after a review's baseline that affects a baselined item is a nonconformance, dispositioned by the nonconformance review board, which is the maintainer (ECSS-Q-ST-10-09 is not applied beyond this). The rms has no nonconformance record yet and the board has no PA member other than the developer (OI-11, OI-14). Nonconformance procedures apply from the first baseline (the PDR). Protection of the supplied software: the SHA-256 of each package in SHA256SUMS and in the consumers' pins; labelling: the package name and version (`libuuid7_<version>_amd64.deb`).

### 6.6 Process metrics

Collected by `ecss/make.sh` at each refresh and reported in the SPAMR <7>: number of problems detected during verification and validation (failed cases), pipeline result per stage, open change requests. Duration and effort are not collected (one maintainer; category D limits 6.2.5.4 to the number of problems detected during validation).

### 6.7 Reuse of software

The library is itself software intended for reuse; its reuse file is the \ref UUID7_DOC_SRF "SRF". It reuses no third-party code; the C library and the toolchain are covered by ECSS-Q-ST-80 5.6 (tools).

### 6.8 Product assurance planning for individual processes and activities

| Process | Assurance activity |
|---|---|
| Requirements analysis | every requirement atomic, with rationale, criteria, method and source; the rms `check` on each; review at SRR/PDR |
| Architectural design and design of items | components and their requirements traced (`@ingroup`, `@satisfies`); review at PDR/CDR |
| Coding | the coding standard's gates; the inspection UUID7-IN-008 |
| Testing and validation (regression included) | every change runs every suite in every cell; coverage measured against the proposed figures (UUID7-SRS-090) |
| Verification | the SVerP's activities; the generated matrices show any gap |
| Delivery and acceptance | packages reproducible and smoke-tested; SRelD and SCF regenerated |
| Operations and maintenance | the SMP; the same gates |

### 6.9 Procedures and standards {#spap_procedures}

| Aspect | Procedure or standard |
|---|---|
| Project management | SDP |
| Risk management | SDP <4.5> |
| Configuration and documentation management | SCMP; DRDs of ECSS-E-ST-40 and ECSS-Q-ST-80 |
| Verification and validation | SVerP, SValP, SUITP, SVS |
| Requirements engineering | SDP <5.3>; the rms (kyr-rms-cli) |
| Design | SDD <4.7>; coding standard |
| Coding | \ref UUID7_DOC_CODING_STANDARD "coding standard" |
| Metrication | this plan <5.5>, <6.6>, <7> |
| Nonconformance control | <6.5> |
| Audits | none planned (category D) |
| Alerts | the maintainer watches the C library's and the toolchain's security notices |
| Procurement | SDP <4.8> |
| Reuse of existing software | SRF |
| Use of methods and tools | SDP <5.4> |
| Numerical accuracy | not relevant: integer arithmetic only (SVR <6>) |
| Delivery, installation and acceptance | SDP <5.5.3>; SUM <9.2> |
| Operations | not applicable (\ref UUID7_DOC_TAILORING "tailoring") |
| Maintenance | SMP |
| Device programming and marking | not applicable |

Adherence is ensured by the gates of the pipeline (each named in the coding standard) and checked at each review by the inspection of the remaining rules.

## 7 Software product quality assurance

1. Product metrics and targets: the table of <5.5>; collected by `ecss/make.sh` from the coverage run, the stress benchmark, the build logs, doxygen and the test logs.
2. Metrication programme: at every refresh, so at every commit that is reviewed or released.
3. Analyses: comparison with the targets; any miss is a problem report.
4. Feedback: the SPAMR and the pipeline output, read by the developer who is also the assurer.
5. Documentation quality: every document follows its DRD and is regenerated from the product; a document out of date cannot exist for a refreshed commit.
6. Assurance activities: the verification cases that check the quality requirements (UUID7-SRS-090, UUID7-SRS-091, UUID7-SRS-060, UUID7-SRS-040).

## 8 Compliance matrix to software product assurance requirements

The matrix is \ref UUID7_DOC_Q80_COMPLIANCE "the ECSS-Q-ST-80 compliance matrix" (requirement identifier, compliance C/PC/NY/NC/NA, reference, remarks).
