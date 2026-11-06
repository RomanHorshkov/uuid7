# Software maintenance plan (SMP) {#UUID7_DOC_SMP}

<!-- ecss:docinfo id=UUID7-DOC-SMP -->

## 1 Introduction

This SMP describes how uuid7 is maintained once delivered: how a problem or a request becomes a released change, who does what, and which records are kept (ECSS-E-ST-40 5.10.2). It is issued for the QR and kept for AR.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Scope and Purpose

Corrective maintenance (defects reported by the consumers or found by the gates), preventive maintenance (toolchain and platform changes, pin bumps) and improvements (new functions as minor versions). The process starts when a request is recorded as an rms change-request and ends when the release carrying it is tagged and its packages published. Maintenance differs from development only in its trigger: the same pipeline, the same documents, the same reviews (QR and AR for the release).

## 5 Application of the Plan

Request (rms change-request) → analysis (impact on requirements, ABI, consumers) → design and implementation on a branch (requirement first, then code and tests with their tags) → `./utils/run_pipeline.sh` green → `ecss/make.sh` → review (QR/AR of the release) → fast-forward of `master`, tag, release → the consumers re-pin. Each step is measured by its gate (pipeline stages, verification status, rms gaps); the expected performance: no release with a red stage or an unverified requirement.

## 6 General Requirements

### 6.1 System

The library and its role: \ref UUID7_DOC_SRS "SRS" <4>; its consumers interoperate through its ABI only (\ref UUID7_DOC_ICD "ICD").

### 6.2 Status

Initial status for maintenance: the version and commit of this package (\ref UUID7_DOC_SRELD "SRelD"); formal name uuid7, packages libuuid7 and libuuid7-dev, soname libuuid7.so.1.

### 6.3 Support

Consumers depend on the library for their keys for as long as they run; defects must be corrected and the toolchain followed.

### 6.4 Maintainer organisation

The maintainer, Roman Horshkov, who also developed it: no transition.

### 6.5 Contracts

None.

## 7 Maintenance Concept

### 7.1 Concept

1. Scope: full: every correction and improvement the maintainer approves.
2. Post-delivery process: this plan, unchanged from development.
3. Maintainer: the developer.
4. Life-cycle cost: the maintainer's time; no licence or service cost.
5. Activities: <8>.

### 7.2 Level of support

Every approved change is released; every request is tracked in the rms; support through the repository.

### 7.3 Support period

From the first release for as long as a consumer pins it.

### 7.4 Tailoring the maintenance process

None beyond the development process's \ref UUID7_DOC_TAILORING "tailoring".

## 8 Maintenance Activities

Correct a defect; follow a toolchain or kyr-build release (re-pin, rebuild in the build root, compare bytes); add a function (minor version, ABI gate); migrate or retire the library when the platform decides it (a migration or retirement plan is issued then, ECSS-E-ST-40 5.10.6, 5.10.7). The user's role: report through the repository.

## 9 Resources, methods and standards

### 9.1 Resources

1. Platforms: development, maintenance and target are the same Linux x86-64; releases in the pinned build root.
2. Differences: the host's toolchain may be newer than the build root's; releases use the build root's.
3. Tools: those of the \ref UUID7_DOC_SDP "SDP" <5.4>, pinned in `deps.txt`.
4. Planning: the same plans (SDP, SValP, SUITP).

### 9.2 Methods and standards

Those of development (SDP <5.3>, \ref UUID7_DOC_CODING_STANDARD "coding standard").

## 10 Maintenance Process {#smp_process}

### 10.1 Description

A request is classified (defect, preventive, improvement) and prioritised (a defect affecting ordering or uniqueness first) on its rms change-request, and assigned to the next release. The software configuration control board is the maintainer. Phases: <10.2> to <10.6>.

### 10.2 Analysis phase

Reproduce or confirm the problem (a failing test first); record acceptance or rejection on the change-request; plan the release; identify constraints (ABI: major or minor; consumers to re-pin); record risks.

### 10.3 Design phase

Revise the requirements in the rms (the links they invalidate turn suspect); revise the design (doc comments, components).

### 10.4 Implementation phase

Code and tests on a branch, tagged with the requirements; the pipeline on every push; risks reviewed as the change grows.

### 10.5 Acceptance phase

The full regression (every suite, every cell), the analyses and the regenerated package; the consumers' pipelines with the new pin are their acceptance.

### 10.6 Delivery phase

Fast-forward `master`, tag `v<VERSION>`, publish the packages; the previous release stays available for rollback; the ECSS package of the release is the backup of the documentation.

## 11 Training Requirements

None: the maintainer is the developer.

## 12 Software Product Assurance Activities

The SPAP's activities apply unchanged; the package's claim (its front page) is recomputed at every refresh and the package is reviewed for every release.

## 13 Software Configuration Management

The SCMP's: problem reports, nonconformances, change requests and waivers are rms artifacts linked to what they affect (`impacts`), closed by the release that implements them.

## 14 Records and Reports {#smp_records}

1. Records: the rms change-requests (problem reports), their audit trail, the git history, the release notes (\ref UUID7_DOC_SRELD "SRelD") and the \ref UUID7_DOC_SPR "problem reports page".
2. Submission: a request is submitted as a repository issue or directly as an rms change-request.
3. Content kept: the requests and their status, the responsible (the maintainer), the priority, the result of each action; statistical failure data is not collected (category D).

## 15 Sample Request Form

An rms change-request:

| Field | Content |
|---|---|
| human_id | `UUID7-CR-nnn` |
| title | one line |
| body | the problem or request, how to reproduce it |
| rationale | why it matters |
| status | proposed → approved / rejected → implemented → verified |
| links | `impacts` to each requirement, component or document it changes |
