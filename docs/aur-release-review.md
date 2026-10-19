# AUR release review

Reviewed for the first `nutsync` AUR push.

## Metadata

- Package: `nutsync`
- Version: `0.6.0-1`
- Source: `https://github.com/gomodb/nutsync/archive/refs/tags/v0.6.0.tar.gz`
- Source SHA-256:
  `53f6171bfed358af4b0e677cf07108dbfa24f3964f27a7e0d8cca6c739d7cbf9`
- License: `GPL-3.0-or-later`
- AUR packaging license: `0BSD`
- Architectures: `x86_64`, `aarch64`

## Checks

- [x] `makepkg --verifysource -f` passed
- [x] `makepkg -f` built a local x86_64 package successfully
- [x] Local package extracts to a test root and reports `nutsync 0.6.0`
- [x] PKGBUILD and generated `.SRCINFO` match
- [x] `bash -n packaging/arch/PKGBUILD` passed
- [x] Source tag exists and checksum matches
- [x] AUR package name `nutsync` is available (RPC result count 0)
- [x] GitHub release has both x86_64 and aarch64 packages
- [x] x86_64 package installs and reports `nutsync 0.6.0`
- [x] CI `AUR package (tag/manual)` passed, including `namcap`
- [x] Desktop file validation passed
- [x] AppStream validation passed
- [x] Local AUR repository is initialized, clean, and remote is configured

## Local AUR repository

```text
~/Projects/nutsync-aur
8e4a518 nutsync 0.6.0-1
origin ssh://aur@aur.archlinux.org/nutsync.git
```

Files are mode `0644`.

## Local build fix

The first local build exposed a stale-cache bug: the PKGBUILD used a shared
`$srcdir/build` directory, so a previous version's CMake cache caused:

```text
CMake Error: The source ... does not match the source ... used to generate cache
```

The build directory is now inside `$srcdir/$pkgname-$pkgver/build`, and
`check()`/`package()` use the same path.

## Blocker

The AUR SSH public key has not been registered:

```text
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBpw077hEh6GPF6rv6Ar73HuRPLk2W5HdA9GGGVOSy4T fnoopv@arch
```

`ssh -T aur@aur.archlinux.org` returns `Permission denied (publickey)`.

## After key registration

```bash
cd ~/Projects/nutsync-aur
git push -u origin master
```

Then verify:

```bash
git clone ssh://aur@aur.archlinux.org/nutsync.git /tmp/nutsync-aur-check
cd /tmp/nutsync-aur-check
makepkg --verifysource
```
