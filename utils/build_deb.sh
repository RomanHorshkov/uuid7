#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
PKG_NAME="uuid7"
STRIP="${STRIP:-strip}"

cd "$ROOT_DIR"

# Build the library artifacts
./utils/build_libs.sh release


# Read version + architecture
VER="$(< VERSION)"
ARCH="$(dpkg --print-architecture)"

# The deb filename, soname chain, and control file all embed VERSION verbatim.
# Refuse anything that is not strict MAJOR.MINOR.PATCH.
if ! [[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'error: VERSION must match MAJOR.MINOR.PATCH (digits only), got: %q\n' "$VER" >&2
    exit 1
fi

# Split version safely (keep IFS local)
IFS='.' read -r MAJOR MINOR PATCH <<< "$VER"

# Prepare package staging dir (kept under build/ so it doesn't pollute the repo root).
STAGE="${ROOT_DIR}/build/pkgroot"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/local/lib" "$STAGE/usr/local/include"

# Install payload into /usr/local (inside the package)
install -m 0644 app/uuid7.h "$STAGE/usr/local/include/uuid7.h"

install -m 0755 "build/release/libuuid7.so.$VER" "$STAGE/usr/local/lib/libuuid7.so.$VER"
"$STRIP" --strip-unneeded "$STAGE/usr/local/lib/libuuid7.so.$VER"
ln -sf "libuuid7.so.$VER" "$STAGE/usr/local/lib/libuuid7.so.$MAJOR"
ln -sf "libuuid7.so.$VER" "$STAGE/usr/local/lib/libuuid7.so"

install -m 0644 build/release/libuuid7.a "$STAGE/usr/local/lib/libuuid7.a"

# Control file
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VER
Section: libs
Priority: optional
Architecture: $ARCH
Maintainer: Roman Horshkov <https://github.com/RomanHorshkov>
Description: uuid7 personal library installed under /usr/local
EOF

# post installation script
# ldconfig hooks so runtime linker sees it immediately
cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
ldconfig
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
ldconfig
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/postrm"

# Verify the staged (stripped) payload still carries the release hardening
# before it gets sealed into a package. A red check kills the build here.
"${ROOT_DIR}/utils/check_hardening.sh" "$STAGE/usr/local/lib/libuuid7.so.$VER"

# Build .deb
DEB="${PKG_NAME}_${VER}_${ARCH}.deb"
fakeroot dpkg-deb --build "$STAGE" "$DEB"

echo
echo "Built complete"

OUT_DIR="${OUT_DIR:-${ROOT_DIR}/build/debs}"
mkdir -p "$OUT_DIR"
mv -f "$DEB" "$OUT_DIR/"

# Refresh checksums next to the deb(s) so consumers can verify what they fetch.
(cd "$OUT_DIR" && sha256sum -- *.deb > SHA256SUMS)
echo "checksums refreshed: $OUT_DIR/SHA256SUMS"

echo "see .deb info with dpkg-deb -c $DEB or dpkg-deb -I $DEB"
echo "moved to $OUT_DIR/"
echo "install with sudo apt install $OUT_DIR/$DEB"
