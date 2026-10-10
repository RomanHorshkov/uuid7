# Software requirements specification (SRS) {#UUID7_DOC_SRS}

<!-- ecss:docinfo id=UUID7-DOC-SRS -->

## 1 Introduction

This SRS is the main part of the technical specification (TS) of uuid7, a C library that generates time-ordered UUIDs of version 7. It states every functional and non-functional requirement the library meets, each with its rationale, its verification method and its upper-level source, so that the library can be verified, validated and reused as a category D software item (ECSS-E-ST-40 5.4.2.1). It is prepared for the PDR. The requirements live in the rms (project `UUID7`), `ecss/TS/requirements.csv` is a snapshot of its export, and this document is generated from the export, so the text here and the rms never differ. Since UUID7-CR-004 the requirements state what a user of the library needs; how the library meets them is the \ref UUID7_DOC_SDD "SDD"'s, and its trade-offs are rms decisions (`UUID7-DEC-nnn`).

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Software overview

### 4.1 Function and purpose

The library issues 16-byte identifiers that sort by creation time (RFC 9562 UUIDv7): a 48-bit Unix millisecond timestamp, a 12-bit sequence that orders identifiers within one millisecond, and 62 random bits. Its purpose on the kyr platform is to key database records so that new records append in time order, across threads and across restarts, without a central counter.

### 4.2 Environmental considerations

1. Physical environment: none of its own; the library runs inside its caller's process.
2. Hardware environment: x86-64, the platform's only target; any CPU with 64-bit atomic compare-and-swap.
3. Operating environment: Linux with glibc (the pinned build root's, see the \ref UUID7_DOC_SCF "SCF"); `getrandom(2)` when present, `/dev/urandom` otherwise.

### 4.3 Relation to other systems

uuid7 is a foundation library (tier 1): it knows nothing of the platform. The platform's libraries and products link it to key their records (kyr_lmdb, kyr_core, kyr_drive, kyr_estate, kyr_rms and the platform manifest pin its packages). It uses the C library only. The package diagram of the SDD shows the build-time dependencies.

### 4.4 Constraints

- C11 and the platform's build tooling (kyr-build), whose profiles, hardening gate and ABI gate the library must pass; the reason: one toolchain and one set of gates for every repository.
- No allocation and no I/O beyond the entropy source; the reason: a foundation library must be usable anywhere its callers are.
- The binary layout is fixed by RFC 9562; the ordering contract is fixed by the consumers' stored keys.

## 5 Requirements

### 5.1 General

Each requirement has a unique identifier `UUID7-SRS-nnn` (the interface items, `UUID7-IF-nnn`, are in the ICD), a verification method (T test, A analysis, I inspection, R review) and an upper-level source: RFC 9562, a platform rule or a platform need. The requirements are characterised by the software criticality category (all D) and the rms lifecycle status with who set it; those statuses come from the scripted rms demonstration (\ref UUID7_DOC_JRR "JRR"), and the rms criticality is a workflow priority, not an ECSS category (\ref tailoring_scales "tailoring <3.1>"). Section <7> gives the traceability to the sources.

### 5.2 Functional requirements

The functional requirements are grouped by subject. The library has a single operating state: there is no mode to link them to.

<!-- ecss:reqs doc=SRS section=5.2 level=4 -->

### 5.3 Performance requirements

<!-- ecss:reqs doc=SRS section=5.3 level=4 groups=no -->

### 5.4 Interface requirements

The external interfaces (the header, its types and functions, the shared object's linkage) are specified in the \ref UUID7_DOC_ICD "ICD". The naming convention of the interface is NAMING.md's: every external symbol starts with `uuid7_`, every constant with `UUID7_` (UUID7-SRS-062).

### 5.5 Operational requirements

The library has no operational mode and no mode transition: initialisation is optional (UUID7-SRS-021), and every call is valid at any time from any thread (UUID7-SRS-028). There is no human-machine interaction.

### 5.6 Resources requirements

The hardware environment is in <4.2>. The software used with the library is the C library only (UUID7-SRS-052). There is no real-time constraint of the library's own: it never waits (UUID7-SRS-030).

<!-- ecss:reqs doc=SRS section=5.6 level=4 groups=no -->

### 5.7 Design requirements and implementation constraints

The applicable coding standard is \ref UUID7_DOC_CODING_STANDARD "the coding standard"; the library is designed for intended reuse (UUID7-SRS-052). It reuses no third-party code; it has no in-flight modification and no numerical accuracy concern (integer arithmetic only).

<!-- ecss:reqs doc=SRS section=5.7 level=4 groups=no -->

### 5.8 Security and privacy requirements

The functional requirements UUID7-SRS-005, UUID7-SRS-017, UUID7-SRS-019, UUID7-SRS-024 and UUID7-SRS-041 also carry security; the security analysis is the \ref UUID7_DOC_SSAR "SSAR".

<!-- ecss:reqs doc=SRS section=5.8 level=4 groups=no -->

### 5.9 Portability requirements

<!-- ecss:reqs doc=SRS section=5.9 level=4 groups=no -->

### 5.10 Software quality requirements

The quality model is the SPAP's (<5.5>); its measurable requirements are:

<!-- ecss:reqs doc=SRS section=5.10 level=4 groups=no -->

### 5.11 Software reliability requirements

<!-- ecss:reqs doc=SRS section=5.11 level=4 groups=no -->

### 5.12 Software maintainability requirements

<!-- ecss:reqs doc=SRS section=5.12 level=4 groups=no -->

### 5.13 Software safety requirements

None: the library implements no safety function and is of criticality category D (\ref UUID7_DOC_TAILORING "tailoring").

### 5.14 Software configuration and delivery requirements

<!-- ecss:reqs doc=SRS section=5.14 level=4 groups=no -->

### 5.15 Data definition and database requirements

The only data exchanged is the 16-byte identifier, specified by UUID7-SRS-001 and UUID7-SRS-005 and stored by the callers as is (a floor is an identifier read back, UUID7-SRS-016). The library has no database.

### 5.16 Human factors related requirements

None: no operator, no manual operation, no human-equipment interaction.

### 5.17 Adaptation and installation requirements

The only adaptation datum is UUID7_MAX_AHEAD_MS (UUID7-SRS-122).

<!-- ecss:reqs doc=SRS section=5.17 level=4 groups=no -->

## 6 Validation requirements

Every requirement is validated by the approach of its method; the cases are specified in the \ref UUID7_DOC_SVS "SVS". The validation matrix:

<!-- ecss:validation-matrix doc=SRS -->

## 7 Traceability

The upper-level sources stand where a requirements baseline would (the RB is tailored out, see \ref UUID7_DOC_TAILORING "the tailoring"). The downward traceability to design and tests is in the DJF: \ref UUID7_DOC_TRACE "the matrices".

<!-- ecss:upward-trace doc=SRS -->

The requirements of the first issue that left the specification under UUID7-CR-004, each with the item that replaced it or the reason it was made obsolete:

<!-- ecss:retired doc=SRS -->

## 8 Logical model description

Not required for category D: ECSS-E-ST-40 Annex R marks 5.4.2.3a to c N. The behaviour is specified by the requirements of <5.2>; the SDD <5.2> describes the reservation rule as a state model.
