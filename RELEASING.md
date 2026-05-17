# Releasing UUID7

This project is set up so a Git tag is the release trigger.

## Release flow

1. Make sure the branch you want to release is green locally.
   Run:
   ```sh
   ./utils/make_libs.sh
   ./utils/make_ITs.sh
   ./utils/make_stress.sh
   ```

2. Bump `VERSION` if the release version changed.

3. Merge the release candidate branch into `master`.

4. Update local `master`.
   Run:
   ```sh
   git checkout master
   git pull --ff-only origin master
   ```

5. Create an annotated tag that matches `VERSION`.
   Example for `VERSION=2.0.0`:
   ```sh
   git tag -a v2.0.0 -m "UUID7 v2.0.0"
   ```

6. Push `master`, then push the tag.
   Run:
   ```sh
   git push origin master
   git push origin v2.0.0
   ```

7. GitHub Actions `release.yml` will:
   - rebuild the libraries
   - build the Debian package
   - assemble a release tarball
   - generate `SHA256SUMS`
   - publish a GitHub Release with the artifacts attached

## Important rules

- The tag must match `VERSION` exactly.
  If `VERSION` is `2.0.0`, the tag must be `v2.0.0`.

- Release tags should always be annotated tags, not lightweight tags.

- Do not tag from a branch that is not the exact code you want on `master`.

## Published release artifacts

Each GitHub Release publishes:

- `uuid7-<version>-linux-x86_64.tar.gz`
- `uuid7_<version>_<arch>.deb`
- `SHA256SUMS`
