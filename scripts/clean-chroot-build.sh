#!/usr/bin/env bash
# Build the current AUR package in a clean archbuild chroot.
# Requires devtools and sudo/root; this is intentionally not run by normal CI.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
chroot="${1:-/var/lib/archbuild/nutsync}"
command -v mkarchroot >/dev/null || {
    echo "devtools is required (pacman -S devtools)" >&2
    exit 1
}
command -v makechrootpkg >/dev/null || {
    echo "makechrootpkg is required (devtools)" >&2
    exit 1
}
if [[ -e "$chroot/root" ]]; then
    echo "Using existing chroot: $chroot"
else
    sudo mkarchroot "$chroot/root" base-devel
fi
cd "$root/packaging/arch"
sudo makechrootpkg -c -r "$chroot" -- --noconfirm
