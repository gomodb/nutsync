#!/usr/bin/env bash
# Refresh sha256sums for the current local Arch package version.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pkgbuild="$root/packaging/arch/PKGBUILD"

pkgver="$(sed -n 's/^pkgver=//p' "$pkgbuild")"
pkgrel="$(sed -n 's/^pkgrel=//p' "$pkgbuild")"
tag="v${pkgver}"
archive="$(mktemp --suffix=.tar.gz)"
trap 'rm -f "$archive"' EXIT

url="https://github.com/gomodb/nutsync/archive/refs/tags/${tag}.tar.gz"
echo "Downloading $url"
curl -fL --retry 5 --retry-delay 2 -sS -o "$archive" "$url"
checksum="$(sha256sum "$archive" | awk '{print $1}')"

python3 - "$pkgbuild" "$checksum" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
checksum = sys.argv[2]
text = path.read_text()
text, count = re.subn(r"^sha256sums=\(.*\)$",
                      f"sha256sums=('{checksum}')", text,
                      count=1, flags=re.MULTILINE)
if count != 1:
    raise SystemExit("sha256sums line not found")
path.write_text(text)
PY

echo "Updated nutsync ${pkgver}-${pkgrel}"
echo "sha256sums=('$checksum')"
