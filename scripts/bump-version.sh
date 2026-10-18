#!/usr/bin/env bash
# Prepare a new version. It does not create or push tags.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "usage: $0 X.Y.Z" >&2
    exit 2
fi

old="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -n1)"
if [[ -z "$old" ]]; then
    echo "could not detect current CMake version" >&2
    exit 1
fi
if [[ "$old" == "$version" ]]; then
    echo "version is already $version" >&2
    exit 1
fi

python3 - "$root" "$old" "$version" <<'PY'
import datetime
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
old = sys.argv[2]
version = sys.argv[3]
today = datetime.date.today().isoformat()

cmake = root / "CMakeLists.txt"
text = cmake.read_text()
text, count = re.subn(r"^(\s*VERSION\s+)" + re.escape(old) + r"$",
                      lambda m: m.group(1) + version, text,
                      count=1, flags=re.MULTILINE)
if count != 1:
    raise SystemExit("CMake VERSION line not found")
cmake.write_text(text)

pkgbuild = root / "packaging/arch/PKGBUILD"
text = pkgbuild.read_text()
text = re.sub(r"^pkgver=.*$", f"pkgver={version}", text, count=1, flags=re.MULTILINE)
text = re.sub(r"^pkgrel=.*$", "pkgrel=1", text, count=1, flags=re.MULTILINE)
text = re.sub(r"^sha256sums=.*$", "sha256sums=('SKIP')", text, count=1, flags=re.MULTILINE)
pkgbuild.write_text(text)

for name in ("README.md", "README.zh-CN.md"):
    path = root / name
    text = path.read_text()
    text = text.replace(f"v{old}.tar.gz", f"v{version}.tar.gz")
    text = re.sub(rf"\b{re.escape(old)}-[0-9]+\b", f"{version}-1", text)
    path.write_text(text)

metainfo = root / "packaging/io.github.gomodb.NutSync.metainfo.xml"
text = metainfo.read_text()
entry = (
    "  <releases>\n"
    f'    <release version="{version}" date="{today}">\n'
    "      <description>\n"
    f"        <p>NutSync {version}.</p>\n"
    "      </description>\n"
    "    </release>\n"
)
if "  <releases>\n" not in text:
    raise SystemExit("AppStream releases block not found")
text = text.replace("  <releases>\n", entry, 1)
metainfo.write_text(text)
PY

(cd "$root/packaging/arch" && makepkg --printsrcinfo > .SRCINFO)
echo "Prepared $old -> $version"
echo "Next: review, commit, tag, then run scripts/update-aur-checksum.sh"
