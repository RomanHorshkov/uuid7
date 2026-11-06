# rms export

What the generator reads when present (ecss/README.md, "The rms"): `package.json`, from `kyr-rms-cli <db> package export --project UUID7`, and optionally `verification.csv`, from `kyr-rms-cli <db> matrix verification --project UUID7`. `build/ecss/rms/export.sh` writes both. Absent, the documents come from the repository's CSVs.
