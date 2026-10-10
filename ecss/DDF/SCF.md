# Software configuration file (SCF) {#UUID7_DOC_SCF}

<!-- ecss:docinfo id=UUID7-DOC-SCF -->

The SCF's DRD is ECSS-M-ST-40 Annex E, which is not held with this package (\ref UUID7_DOC_TAILORING "tailoring" <7>). This file gives what ECSS-E-ST-40 and ECSS-Q-ST-80 ask of it: the identification and contents of the configuration item, its build procedure (5.5.3.1a eo b), its integrity values (ECSS-Q-ST-80 6.2.4.10) and its labels; it will be restructured on the DRD.

## Identification {#scf_labels}

Configuration item uuid7, repository `RomanHorshkov/uuid7`, version `VERSION`, identified in the source by the commit and tag below and in the product by the package names and SHA-256 (source code label and product label, ECSS-E-ST-40 Table A-1). The evidence of this package is of the commit below, produced in the environment below; any disagreement (a host build, uncommitted changes, packages or baselines of another commit) is a configuration flag:

<!-- ecss:configuration -->

<!-- ecss:release -->

Release tags:

<!-- ecss:tags -->

The rms baselines and the commits they froze:

<!-- ecss:baselines -->

## Build procedure

From a clean checkout of the tag, without root:

~~~{.sh}
./utils/deps.sh sysroot && eval "$(./utils/deps.sh env)"   # the pins of deps.txt into build/sysroot (SHA-256 verified)
./utils/run_pipeline.sh                                   # every gate; then the packages:
./utils/build_deb.sh                                      # build/debs: libuuid7, libuuid7-dev, SHA256SUMS, .buildinfo
~~~

Releases are built inside the pinned build root (`build/sysroot/share/kyr-build/build_root.sh enter -- ./utils/build_deb.sh`), whose identity each `.buildinfo` records; `SOURCE_DATE_EPOCH` comes from the commit, so the same commit gives the same bytes (UUID7-SRS-121).

## Build environment

<!-- ecss:toolchain -->

The pins of `deps.txt`:

<!-- ecss:deps -->

## Receiving inspection {#scf_receiving}

<!-- ecss:receiving -->

## Contents

<!-- ecss:scf-items -->
