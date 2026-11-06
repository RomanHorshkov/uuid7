# Software product assurance milestone report (SPAMR) {#UUID7_DOC_SPAMR}

<!-- ecss:docinfo id=UUID7-DOC-SPAMR -->

## 1 Introduction

This SPAMR reports the product assurance activities performed on uuid7 up to the commit it is generated from, for the milestone review held on that commit (ECSS-Q-ST-80 5.2.2.3). Every figure below is measured by `ecss/make.sh` on that commit; frozen with the review's rms baseline, it is the milestone's report.

## 2 Applicable and reference documents

<!-- ecss:refs -->

## 3 Terms, definitions and abbreviated terms

<!-- ecss:terms -->

## 4 Verification activities performed

1\. Reviews: the reviews recorded so far in the rms and their outcome (a scripted demonstration, \ref UUID7_DOC_JRR "JRR"):

<!-- ecss:reviews -->

2\. Inspections and analyses: the non-test verification cases and their results are in the \ref UUID7_DOC_SVR "SVR" <4> and the \ref UUID7_DOC_SVS "SVS" <9>.

3\. Walk-throughs: the scenario sequence diagrams (SUM <9>, SDD <5.2>, SVS <7>) are the walk-through of the behaviour; each is the record of a passing test.

4\. Traceability matrices (ECSS-Q-ST-80 6.2.6.12): verified by generation at this refresh; the gaps:

<!-- ecss:gaps -->

5\. Documents reviewed: the data package of the review (\ref UUID7_DOC_SREVP "SRevP" <10>).

Measures for critical software: none required (category D, no critical component).

## 5 Methods and tools

The methods and tools of the SDP <5.3> and <5.4> are justified in the \ref UUID7_DOC_SPAP "SPAP" <5.8>; `ecss/tools/ecss.py` and kyr_rms have no prior use and are cross-checked instead. Their correct use in this refresh (ECSS-Q-ST-80 5.6.1.3), tool by tool:

<!-- ecss:tools columns=tool,version,output_checked_by -->

The gates themselves (a tool misused fails its stage):

<!-- ecss:pipeline -->

## 6 Adherence to design and coding standards

The coding standard's gated rules hold when the stages `format`, `template`, `abi`, `abi-exports` and the analyses below pass; the remaining rules were inspected (\ref tc_UUID7_IN_008 "UUID7-IN-008"). Finding of the inspection: `uuid7_clock_failure_count()` is an accessor named by its noun, against rule N3 (functions are verbs); renaming it changes the ABI (a major), so it is kept (use as is). It is a deviation without a nonconformance record: the rms has no nonconformance kind yet (\ref UUID7_DOC_SDP "SDP" <4.6>).

<!-- ecss:checks -->

1. Complexity and modularity: seven components of at most a few functions each (\ref UUID7_DOC_SDD "SDD" <5.3>); the longest function, uuid7_gen(), is one reservation loop and one encoding.
2. Design documentation for maintenance: the doc comments of every symbol and the generated SDD; checked by the documented-interface analysis above.

## 7 Product and process metrics

<!-- ecss:metrics -->

Software maturity analysis: not required at category D (ECSS-Q-ST-80 7.1.8: N). No corrective action is open.

## 8 Testing and validation {#spamr_testing}

Adequacy of the test documentation: the \ref UUID7_DOC_SUITP "SUITP" and the \ref UUID7_DOC_SVS "SVS" are generated from the tests themselves, so they are complete and traceable by construction; procedures and data were verified by sample (category D). Achieved coverage against the agreed goals:

<!-- ecss:coverage -->

Statement of compliance with test plans and procedures (ECSS-Q-ST-80 6.3.5.11): the tests ran as the plans state, on the configuration identified in the \ref UUID7_DOC_SCF "SCF", from the repository's own wrappers; the reports are those of this refresh:

<!-- ecss:summary -->

## 9 SPRs and SW NCRs

<!-- ecss:spr -->

## 10 References to progress reports

No separate progress report: the pipeline output of each commit and this report are the progress reporting.
