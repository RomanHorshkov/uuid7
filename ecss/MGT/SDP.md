# Software development plan (SDP) {#UUID7_DOC_SDP}

<!-- ecss:docinfo id=UUID7-DOC-SDP -->

## 1 Introduction

This SDP describes how uuid7 is managed and developed: the life cycle, the reviews, the standards, the environment and the documentation, for a category D library developed for intended reuse by one maintainer. It is issued for the SRR and the PDR (ECSS-E-ST-40 5.3.2) and kept current by `ecss/make.sh`, which regenerates every plan with the product.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Software project management approach

### 4.1 Management objectives and priorities

In order: correctness of the ordering contract (no duplicate, no misordered identifier); compatibility for the consumers (ABI and SemVer); a release that any machine rebuilds to the same bytes; the smallest code that does it.

### 4.2 Master schedule

uuid7 follows the platform's release train (REFACTORING_PLAN.md, the platform's master schedule); there is no date of its own. A milestone is a release: the reviews of the \ref UUID7_DOC_SREVP "SRevP" are held on the commit about to be tagged.

### 4.3 Assumptions, dependencies and constraints

1. Assumptions: the consumers keep using the binary interface; Linux x86-64 stays the only target.
2. External dependencies: the pinned kyr-build (tooling), the distribution's gcc, cmocka and gcovr; the rms for requirements management.
3. Constraints: one maintainer; no customer contract; the platform's rules (NAMING.md, DEPENDENCIES.md, the kyr-build template) are not negotiable per repository.
4. Technical issues: none open.

### 4.4 Work breakdown structure

| Work package | Activities | Outputs |
|---|---|---|
| WP1 Requirements | maintain the requirements in the rms, review them | TS (SRS, ICD) |
| WP2 Design and code | design, code, doc comments, tags | app/, SDD, SUM |
| WP3 Verification and validation | suites, scenarios, analyses, inspections, the pipeline | DJF |
| WP4 Configuration and release | versions, packages, tags | SCF, SRelD, debs |
| WP5 Product assurance | plans, metrics, compliance | PAF |
| WP6 Maintenance | problem reports, modifications | MF |

### 4.5 Risk management

The software engineering contribution is the identification of technical risks at each review, recorded as rms artifacts (decision, change-request). Open risks: none. Identified risk: a toolchain change that alters the binary (mitigated by the pinned build root and the reproducibility check, SVR).

### 4.6 Monitoring and controlling mechanisms

The pipeline result on every commit, the regenerated SPAMR metrics at every milestone, and the rms gaps report (requirements without passing verification) are the monitoring. Action items are rms change requests.

### 4.7 Staffing plan

One maintainer, Roman Horshkov, holds every role: developer, verifier, configuration manager, product assurance, reviewer. The skills are C11, POSIX, C11 atomics, the platform's tooling and ECSS documentation; they are in use across the platform's repositories. Independence is not required at category D (ECSS-Q-ST-80 5.1.3.2 and 6.3.5.19: N).

### 4.8 Software procurement process {#sdp_procurement}

No software is procured from a supplier. The procured components are tools and the C library, each pinned in `deps.txt` and verified on arrival:

<!-- ecss:deps -->

Ordering criterion: the distribution's package or the platform's own release; receiving inspection: `utils/deps.sh` (SHA-256 of the kyr-build deb, see \ref scf_receiving "the receiving inspection"); back-up: the deb cache `~/.cache/rh-deps` and the pinned build root; country source: Ubuntu archive and GitHub releases.

### 4.9 Supplier management

No supplier.

## 5 Software development approach

### 5.1 Strategy to the software development

The library was written first and is now specified, verified and documented against ECSS at category D; from now on every change goes requirement first (rms), then code with its tags, then tests, then the pipeline, then the regenerated documents.

<!-- ecss:diagram name=pipeline -->

### 5.2 Software project development life cycle

#### 5.2.1 Software development life cycle identification

Incremental: each release is a full pass requirement → design → code → verification → validation → delivery, small enough to be one change set. Versions are SemVer (`VERSION`): an ABI change is a major, a new public name a minor, anything else a patch; the soname major equals the SemVer major (NAMING.md). Every process of ECSS-E-ST-40 is covered: the system requirement process is tailored out (\ref UUID7_DOC_TAILORING "tailoring"); requirements and architecture (TS, SDD); design and implementation (code, SUM); validation (SValP, SVS); verification (SVerP, SVR); delivery and acceptance (packages, SRelD); operation (tailored out); maintenance (\ref UUID7_DOC_SMP "SMP"); management (this plan). Development hands over to maintenance at the AR: the same repository, tools and documents.

#### 5.2.2 Relationship with the system development cycle

The platform has no ECSS system life cycle. A uuid7 release precedes the platform releases that pin it: its QR and AR are held before the consumers re-pin.

#### 5.2.3 Reviews and milestones identification and associated documentation {#sdp_interfaces}

The reviews (SRR with SWRR and PDR, CDR with DDR, QR, AR) are defined in the \ref UUID7_DOC_SREVP "SRevP": scope, data package, entry and success criteria, roles. Formalism: a review is the maintainer's walk-through of the data package against the SRevP checklists, recorded as an rms review artifact and closed by an rms baseline. Interface management: the ABI gate (`abi/layout.txt`, `utils/check_abi.sh`) refuses any interface change that the version does not announce; an interface change is a change request reviewed with its consumers.

<!-- ecss:reviews -->

### 5.3 Software engineering standards and techniques

| Process | Method or standard |
|---|---|
| Requirements | atomic "shall" statements with rationale, criteria, method and source, in the rms (kyr-rms-cli); the rms quality check (`check`) is advisory |
| Architecture and detailed design | functional decomposition into components = doxygen groups; the doc comments of the code are the detailed design |
| Coding | \ref UUID7_DOC_CODING_STANDARD "the coding standard" |
| Verification | the SVerP; requirements tagging (`@satisfies`, `@verifies`) and generated traceability |
| Validation | the SValP; CMocka suites; scenario tests that record sequence diagrams |
| Configuration management | git; \ref UUID7_DOC_SCMP "SCMP" |
| Documentation | ECSS DRDs; Markdown sources with generation directives; doxygen |

No part of the software is generated automatically (5.3.2.4 and 5.3.5e not applicable). The delivery format is the Debian package pair libuuid7 / libuuid7-dev with SHA256SUMS and one .buildinfo per package.

### 5.4 Software development and software testing environment

The development and test environment is fixed by `deps.txt` and resolved by `utils/deps.sh sysroot` into `build/sysroot` without root; releases are built inside the pinned build root (Ubuntu 24.04 snapshot, kyr-build's `build_root.sh`) so that every machine produces the same bytes. C11 is the language: portable, close enough to the hardware for lock-free atomics, and the platform's language; no assembly.

| Need | Tool |
|---|---|
| Compiler | gcc (the build root's 13.3; the host's) with the kyr-build profile catalog: debug, audit (-fanalyzer), sanitize (ASan, UBSan, LSan), release, native, extreme, coverage |
| Requirements analysis | kyr-rms-cli (rms) |
| Image generation | kyr-build lib/build.sh and lib/deb.sh (archive, shared object with version script, debs) |
| Configuration management | git, GitHub releases |
| Design | doxygen, graphviz, PlantUML |
| Static analysis | the catalog's warning set, gcc -fanalyzer (audit profile), clang-format 18 |
| Test scripts | C with CMocka; bash for the pipeline |
| Testing | CMocka, gcovr, the sanitizers, the stress benchmarks, kyr-build pipeline.sh |

### 5.5 Software documentation plan

#### 5.5.1 General

Every document is a Markdown source under `ecss/<file>/`, following its DRD, with generation directives that `ecss/tools/ecss.py` fills from the rms and the repository; `ecss/make.sh` writes the Markdown to `build/ecss/md` and one doxygen site to `build/ecss/html`.

#### 5.5.2 Software documentation identification

<!-- ecss:doc-plan -->

#### 5.5.3 Deliverable items

Delivered: the two Debian packages, SHA256SUMS, one .buildinfo per package, the tagged source, and this qualification package (the site). Internal: the rms database and its export, the pipeline logs. Each delivery is from the maintainer (supplier) to the consuming repositories (customers): purpose, functionality and maturity are stated in the \ref UUID7_DOC_SRELD "SRelD" (verification status, known problems); the delivery review is part of QR/AR.

#### 5.5.4 Software documentation standards

The DRDs of ECSS-E-ST-40 Annexes D to P and T and of ECSS-Q-ST-80 Annexes B and C, tailored as \ref UUID7_DOC_TAILORING "the tailoring" states; READMEs terse and never hard-wrapped.

### 5.6 This Standard's tailoring traceability

The coverage matrix of ECSS-E-ST-40 clause 5 is \ref UUID7_DOC_E40_COMPLIANCE "the ECSS-E-ST-40 compliance matrix".
