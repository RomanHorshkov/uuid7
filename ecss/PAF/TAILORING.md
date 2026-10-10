# Tailoring statement and criticality classification {#UUID7_DOC_TAILORING}

<!-- ecss:docinfo id=UUID7-DOC-TAIL -->

## 1 Purpose

The owner asked for uuid7 to be documented "as per ECSS standard, level 4". ECSS defines no levels: it defines software criticality categories A to D (ECSS-Q-ST-80 Annex D.1, Table D-1) and tailors both standards by them (ECSS-E-ST-40 Annex R, ECSS-Q-ST-80 Annex D.2). This statement fixes the interpretation, classifies the software, and declares, clause by clause, what applies, what is tailored out and why. It also disposes of every item of the document requirements list (ECSS-E-ST-40 Annex A, Table A-1).

## 2 Interpretation of "level 4"

"Level 4" is read as the fourth and lowest criticality category, **D**. The owner still wants the full document set, so every DRL item that applies to a reusable library at category D is produced, each following its DRD section by section; a section the tailoring empties stays in place and says why.

## 3 Criticality classification

ECSS-Q-ST-80 6.2.2.1 (applicable at D) asks for the criticality classification of the software products from the system-level analyses (ECSS-Q-ST-30 5.4, ECSS-Q-ST-40 6.5.6.3). uuid7 has no system of its own; the classification is made against its consumers' use:

- Function: it issues identifiers that key records. Its worst credible failure is a duplicate or misordered key, which the consumers' databases refuse or detect (a duplicate key is a refused write; an append out of order is a slower write). No safety function, no mission function, no loss of data follows from it.
- Table D-1: software involved in category IV functions (negligible consequences) with no compensating provision is category D. The library is classified **D**; every component of the \ref UUID7_DOC_SDD "SDD" inherits D, and no component can cause the failure of a higher-criticality component through shared resources (ECSS-Q-ST-80 6.2.2.10): it holds no lock, allocates nothing and writes only into the caller's buffer.
- Security sensitivity: assessed by the \ref UUID7_DOC_SSAR "SSAR": not security sensitive (the identifiers are not secrets; ECSS-Q-ST-80 6.2.9 and 6.2.10 are applied at the level the SSAR justifies).

## 4 Tailoring of ECSS-E-ST-40C Rev.1

Annex R Table R-1 applies as written for category D. In addition, by agreement (Annex S, general tailoring):

| Clauses | Disposition | Justification |
|---|---|---|
| 5.2 (software related system requirement process) | not performed | Its outputs are the customer's ("The customer shall derive system requirements allocated to software", 5.2.2.1a). uuid7 is developed for intended reuse with no customer contract above it; the requirements cite their upper-level sources (RFC 9562, the platform's rules, the consumers' needs) in the SRS <7>. Consequently no RB: no SSS, no IRD, no SRR approval of a requirements baseline (5.3.4.1a), no validation against the RB (5.6.4, 5.8.3.8a eo a, 5.8.3.8b eo b), no traceability of acceptance tests to the RB (5.7.3.5a). |
| 5.7.3.1, 5.7.3.2 (acceptance test plan and execution) | customer's | "The customer shall"; each consuming repository pins a release only after its own pipeline passes with it. The supplier's installation verification is the installed-package smoke test. |
| 5.9 (software operation process) | not performed | A library has no operation of its own: it runs inside its consumers' processes, whose operation process covers it (4.2.9: operations are determined at system level). No OP file. Annex R keeps 5.9 for category D; this is the one tailoring beyond Annex R. |
| 5.3.6.1, 5.3.6.2, 5.4.2.2 | not applicable | Neither flight nor ground-segment software; no in-flight modification. |
| 5.3.2.4, 6.2.8 of ECSS-Q-ST-80 | not applicable | No automatically generated code. |
| Reviews | combined | SRR, SWRR (5.4.2.4) and PDR are held as one review; DDR (5.5.2.10) and CDR as one; the software delivery review board (5.3.5.3) as part of QR and AR (\ref UUID7_DOC_SREVP "SRevP"). |

The clause-by-clause coverage (SDP <5.6>, 5.3.9) is \ref UUID7_DOC_E40_COMPLIANCE "the ECSS-E-ST-40 compliance matrix".

## 5 Tailoring of ECSS-Q-ST-80C Rev.2

Annex D Table D-2 applies as written for category D, including its conditions (training expected output not required, audits only when necessary, relevant quality characteristics only, no formal unit and integration testing, test procedures verified by sample, statistical maintenance data not collected). Clauses addressed to lower-level suppliers, procurement of developed software, ground hardware and programmable devices do not apply: there are none. The clause-by-clause compliance (SPAP <8>) is \ref UUID7_DOC_Q80_COMPLIANCE "the ECSS-Q-ST-80 compliance matrix".

## 6 Document requirements list

Every DRL item of ECSS-E-ST-40 Table A-1 (which includes the ECSS-Q-ST-80 items), with its disposition:

<!-- ecss:drl -->

## 7 DRD tailoring

The DRDs apply without change, with three category-D omissions that Annex R states: the SRS logical model <8> (5.4.2.3 N), the unit test plan's sections 9 to 11 (5.5.2.9a), and the SVR sections whose clauses are N at D (5.8.3.1, 5.8.3.2a eo b, 5.8.3.3a eo b, 5.8.3.4, 5.8.3.5a, 5.8.3.5f, 5.8.3.6, 5.8.3.7, 5.8.3.10, 5.8.3.11, 5.8.3.13). Each document keeps the omitted sections as headings that cite the clause. The SCMP and the SCF have their DRDs in ECSS-M-ST-40, which is not held with this package: they state the practice in force and will be aligned with the DRD when the standard is available.
