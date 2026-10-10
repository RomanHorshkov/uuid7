# Software release document (SRelD) {#UUID7_DOC_SRELD}

<!-- ecss:docinfo id=UUID7-DOC-SRELD -->

## 1 Introduction

This SRelD describes the version of uuid7 this package is generated for: what it contains, what changed since the previous release, its known problems and how to use it (ECSS-E-ST-40 5.7.2.1a). It is issued for QR and AR, with the packages.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Software release overview

1. Configuration file: the \ref UUID7_DOC_SCF "SCF" of the same refresh.
2. Version of the configuration item, packages and changes:

<!-- ecss:release -->

3. Status of problem reports, change requests and waivers: see <5.2>.
4. Advice for use: see <6>.

## 5 Status of the software configuration item

### 5.1 Evolution since previous version

The changes are the commits listed in <4>. The library's behaviour and ABI are unchanged by this documentation work (the `abi` and `abi-exports` stages pass against the committed `abi/layout.txt`); the header gained doc comments only.

Verification status of this version:

<!-- ecss:summary -->

### 5.2 Known problems or limitations

<!-- ecss:spr -->

Limitations by design: identifiers are binary only (formatting is the caller's); they disclose their creation time; they are unique per process, not across processes that share no floor (the 62 random bits make a cross-process collision negligible, not impossible); the logical clock may run up to `UUID7_MAX_AHEAD_MS` ahead of the wall clock under sustained bursts. No waiver.

## 6 Advice for use of the software configuration item

Pin both packages at this exact version with their SHA-256 (DEPENDENCIES.md); a new minor adds functions, a new major changes the ABI and the soname. Import the newest stored identifier at start (\ref UUID7_DOC_SUM "SUM" <9.5>). Compatible with every consumer built against libuuid7.so.1.

## 7 On-going changes

None planned. Candidate evolutions are rms change-requests.
