# The ECSS pipeline

uuid7 documented as category D software after ECSS-E-ST-40C Rev.1 and ECSS-Q-ST-80C Rev.2 (the owner's "level 4", see `PAF/TAILORING.md`). The standards are cited, never copied.

Claim: documented and self-verified at category D; tailoring proposed, not agreed; the review workflow demonstrated in kyr rms with a scripted cast; not independently reviewed; not accepted (AR not held). The front page recomputes it at every run; `MGT/open_items.csv` (SDP <4.6>) says what closes each part.

## One click

```sh
ecss/make.sh               # every gate, the evidence, the diagrams, the documents, the site
ecss/make.sh --docs-only   # keep the last run's results, regenerate the documents and the site
ecss/make.sh --serve       # serve the built site on http://127.0.0.1:8790/ (a sandboxed browser cannot follow file:// links)
```

Output: `build/ecss/md/` (the documents as Markdown), `build/ecss/html/index.html` (one doxygen site: the package's front page with its claim, the documents, the API reference, the traceability, the diagrams, the evidence). Needs python3, doxygen and graphviz; java and the PlantUML jar (`PLANTUML_JAR`, default `/home/rh/plantuml/plantuml-mit-1.2025.10.jar`) render every diagram to SVG (`-nometadata`, the title moved to the caption; the same source gives the same bytes); without them make.sh says CANNOT RENDER and the pages embed the sources. Set `CLANG_FORMAT` to a clang-format 18 for the format stage. Exit 1 if a gate failed; the documents say which.

## From requirement to evidence

1. A requirement is an rms artifact (`UUID7-SRS-nnn`, `UUID7-ICD-nnn`) with rationale, criteria, method (test, analysis, inspection, review) and source. Seed and fallback: `TS/requirements.csv`.
2. The code names what it implements: `@ingroup uuid7_cmp_*` (the SDD component) and `@satisfies UUID7-SRS-nnn` on every documented symbol of `app/`.
3. Every test names what it verifies: `@verifies UUID7-SRS-nnn` (`tests/ITs`, `tests/plantuml`, `tests/stress/stress.c`, `tests/package/smoke.c`); the other verification cases (analyses, inspections, reviews, pipeline gates) are `DJF/verification_cases.csv`.
4. `tests/plantuml` holds scenario tests: real tests that assert, and through `puml_trace.h` write one sequence diagram each to `build/ecss/puml/`. They run in the integration matrix like the other suite.
5. `ecss/make.sh` runs `utils/deps.sh sysroot` and `utils/run_pipeline.sh` (tiers, template, format, ABI, six profiles, both suites in every profile x linkage, coverage, stress, packages, package smoke), rebuilds the packages to prove reproducibility, runs the hardening gate, then `tools/ecss.py`: the model (rms export or CSVs, plus the tags and every result), the class, package and component diagrams, the Markdown documents (each source here, with `<!-- ecss:... -->` directives filled in), the rms loader; then doxygen (`Doxyfile`; `tools/doxy_filter.py` turns the requirement ids of the tags into links).
6. The documents are reviewed at the milestones; a review closes with an rms baseline. The reviews in the rms so far are a scripted demonstration (`DJF/JRR.md` names who the export says held them).

## Links

Every reference is a link (`tools/refs.py`): a section (`<9.7>`, `SRS <5.2>`), a document name or id, a requirement, component, verification case, review, RID, change request, open item or baseline, an ECSS clause (its row of the compliance matrix), a tool (its row of the tools register), an evidence or source file, an API symbol, a test. The links gate (`tools/links_gate.py`) reads the built HTML and turns make.sh RED on any reference left bare and any anchor doxygen cannot resolve, each listed with its page (`build/ecss/links.txt`). Headings, the anchored cell of a register row and code blocks are definitions or code, not references.

## Figures and tables

Every diagram is a numbered figure and every table a numbered table of its document (`Figure N`, `Table N`, caption and anchor; the numbers follow the reading order, so they are stable while the document is). A figure's caption says what produced it: the scenario test that recorded the run, ecss.py, or the hand-written source. Each document lists its figures and tables under its identification; the front page links the package's two lists. In a template a figure is `<!-- ecss:diagram name=... -->`, a table takes its title from a `Table: title {#id}` line above it (else from its heading), and a citation is `{{fig:name}}`, `{{fig:SVS:name}}` or `{{tab:id}}`, written out as a linked `Figure 3`, `SVS Figure 3`, `Table 2`.

## Documents

| File | Documents | Directory |
|---|---|---|
| MGT | SDP (with the ECSS-E-ST-40 compliance matrix and the open items, `open_items.csv`), SRevP, SCMP | `MGT/` |
| TS | SRS, ICD, the requirements | `TS/` |
| DDF | SDD, SUM, SRelD, SCF | `DDF/` |
| DJF | SVerP, SValP, SUITP, SVS, SVR, SRF, design trade-offs and justification (DTJ), unit test report, validation report, traceability matrices, problem reports, joint review reports | `DJF/` |
| PAF | SPAP (with the SSMP, the ECSS-Q-ST-80 compliance matrix and the tools register, `tools.csv`), SPAMR, tailoring and criticality, coding standard | `PAF/` |
| MF | SMP | `MF/` |
| SF | SSAR with the risk treatment plan | `SF/` |
| RB, OP | tailored out, justified in `PAF/TAILORING.md` | - |

`documents.csv` is the register: every DRL item of ECSS-E-ST-40 Table A-1 with its DRD, reviews, status and disposition; the front page is built from it.

## Which review needs which document

| Review | Data package |
|---|---|
| SRR (held with SWRR and PDR) | SDP, SRevP, SCMP, SPAP and SSMP, SVerP, SRF, SSAR, SVR, SPAMR, tailoring |
| PDR | SRS, ICD, SDD, DTJ, SCF, SValP, SDP, SPAP, coding standard, SVR, SPAMR |
| CDR (held with DDR) | ICD, SDD, DTJ, SCF, SUM, unit test plan and report, SVS, validation report, SVR, SPAMR |
| QR | SCF, SRelD, SUM, SMP, SVR, SPAMR |
| AR | SCF, SRelD, SUM, SMP, installation report, SVR, SPAMR |

The complete lists, with status, are on the front page and in `MGT/SRevP.md`.

## The rms

Requirements, components, verification cases, reviews, links, results and baselines live in the rms (`kyr-rms-cli`, project `UUID7`); the documents are generated from its export plus the repository's own facts. The CSVs here use its import format (`TS/requirements.map.json` maps every column; one map for all of them).

Round trip (the project is created by a platform OWNER, who grants the loading actor, a platform ADMIN, the project roles `author` and `config-master`: `kyr-rms-cli <db-root> member grant <uid> author,config-master --project UUID7 --actor <owner-uid>`; imports and baselines need ADMIN and config-master, links and results author):

```sh
ecss/make.sh                                            # also writes build/ecss/rms/: artifacts.csv, links.tsv, results.tsv, load.sh, export.sh
build/ecss/rms/load.sh <db-root> <actor-uid>            # import the artifacts, create the links, record this run's results
kyr-rms-cli <db-root> baseline PDR --project UUID7 --actor <actor-uid>   # at the review
build/ecss/rms/export.sh <db-root> <actor-uid>          # package export -> rms_export/package.json, matrix verification -> rms_export/verification.csv
ecss/make.sh                                            # now generated from the rms; repository links missing there are reported
```

An export in which a decision (approval, review outcome, RID disposition, verification) or a baseline names no actor is refused: RED, exit 1, the front page says why. Each baseline must name the uuid7 commit it froze (a `commit` field, or the commit in its description), else it is a configuration flag. `ECSS_RMS_EXPORT=<dir>` reads another export.

Design decisions are rms artifacts of kind `decision` (alternatives and consequences as `Alternatives:` / `Consequences:` paragraphs of the body or `alternatives=` / `consequences=` notes), with `rationale` and `assumption` artifacts linked to them; any link joins a decision to the requirements it answers, the components it shapes and the verification cases that justify it. `DJF/DTJ.md` is generated from them (empty, with a note, while the export has none), the SDD <4.7> summarises them, and the SRS and SDD name them back. A document of `documents.csv` the export does not hold is produced anyway and listed as drift.

Without `rms_export/package.json` the generator uses the CSVs and the tags. With it, the rms's artifacts, links and baselines are the truth, the fresh run's results are shown with the rms's recorded verdicts, and any tag or note the rms does not hold is listed as drift in `DJF/TRACE.md`.
