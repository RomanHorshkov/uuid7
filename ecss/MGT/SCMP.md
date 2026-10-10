# Software configuration management plan (SCMP) {#UUID7_DOC_SCMP}

<!-- ecss:docinfo id=UUID7-DOC-SCMP -->

The SCMP's DRD is ECSS-M-ST-40 Annex A, which is not held with this package (\ref UUID7_DOC_TAILORING "tailoring" <7>). Until it is, this plan states the configuration management in force, covering what ECSS-Q-ST-80 6.2.4 requires of it, and will be restructured on the DRD.

## Configuration items

One configuration item: uuid7, identified by its repository (`RomanHorshkov/uuid7`) and its version (`VERSION`, SemVer). Its contents are every file under git (the \ref UUID7_DOC_SCF "SCF" lists them with their SHA-256). The rms project `UUID7` holds the requirements, links, results, reviews and baselines; its package export is committed under `ecss/rms_export/` at each review.

## Identification and baselines

- Every change is a git commit; a release is an annotated, signed tag `v<VERSION>` on a commit whose pipeline is green on `master` (NAMING.md).
- A review closes with an rms baseline named after it (`PDR`, `CDR`, ...) and, for QR/AR, the release tag; the baseline package (`kyr-rms-cli package baseline <NAME>`) carries a SHA-256 per section and reproduces byte for byte.
- Any reference version is regenerated from the tag: the source from git, the packages by `utils/build_deb.sh` in the pinned build root, byte-identical (ECSS-Q-ST-80 6.2.4.2).

## Change control

Branch → local pipeline green → push → fast-forward of `master`; no merge commit, no fix-up commit on a published branch. A change to a baselined requirement is an rms revision; the links it invalidates turn suspect and are re-confirmed or re-verified before the next baseline. Authorized changes are the commits between two tags (\ref UUID7_DOC_SRELD "SRelD").

## Branching and merging

`master` is the only long-lived branch; work branches are short and rebased on `master` before the fast-forward (ECSS-Q-ST-80 6.2.4.12).

## Status accounting

`git log`, the rms audit trail (every write with its actor and transaction) and the generated SCF and SRelD of each refresh.

## Protection and integrity

Git's content addressing, the GitHub release's SHA256SUMS and one .buildinfo per package (the build root's identity included); consumers pin each deb by SHA-256 in their `deps.txt` (ECSS-Q-ST-80 6.2.4.8 to 6.2.4.10).

## Controlled documents

The plans, the procedures and standards, and the product documents of this package are controlled as files of the configuration item; their status is their rms document artifact's (ECSS-Q-ST-80 6.2.4.7).
