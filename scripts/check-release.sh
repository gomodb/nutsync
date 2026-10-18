#!/usr/bin/env bash
# Validate that a release tag, CMake version, PKGBUILD, AppStream and .SRCINFO agree.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tag="${1:-}"
if [[ -z "$tag" ]]; then
    echo "usage: $0 vX.Y.Z" >&2
    exit 2
fi
version="${tag#v}"

cmake_version="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -n1)"
pkgver="$(sed -n 's/^pkgver=//p' "$root/packaging/arch/PKGBUILD")"
pkgrel="$(sed -n 's/^pkgrel=//p' "$root/packaging/arch/PKGBUILD")"
appstream_version="$(sed -n 's/.*<release version="\([0-9][0-9.]*\)".*/\1/p' "$root/packaging/io.github.gomodb.NutSync.metainfo.xml" | head -n1)"
checksum="$(sed -n "s/^sha256sums=('\([^']*\)').*/\1/p" "$root/packaging/arch/PKGBUILD")"

test "$version" = "$cmake_version" || {
    echo "tag $tag does not match CMake VERSION $cmake_version" >&2
    exit 1
}
test "$version" = "$pkgver" || {
    echo "tag $tag does not match PKGBUILD pkgver $pkgver" >&2
    exit 1
}
test "$version" = "$appstream_version" || {
    echo "tag $tag does not match AppStream release $appstream_version" >&2
    exit 1
}
[[ "$checksum" =~ ^[0-9a-f]{64}$ ]] || {
    echo "PKGBUILD sha256sums is not a final checksum" >&2
    exit 1
}
test -s "$root/packaging/arch/.SRCINFO" || {
    echo ".SRCINFO is missing" >&2
    exit 1
}

generated="$(mktemp)"
trap 'rm -f "$generated"' EXIT
(cd "$root/packaging/arch" && makepkg --printsrcinfo > "$generated")
diff -u "$root/packaging/arch/.SRCINFO" "$generated"

echo "Release ${version}-${pkgrel} metadata is consistent"
