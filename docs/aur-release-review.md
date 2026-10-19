# AUR release review

Reviewed for the first `nutsync` AUR push.

## Metadata

- Package: `nutsync`
- Version: `0.5.2-1`
- Source: `https://github.com/gomodb/nutsync/archive/refs/tags/v0.5.2.tar.gz`
- Source SHA-256:
  `fed7030255d3d3289dfa085aef4903d37f08305da39c39395f57f19620beb31b`
- License: `GPL-3.0-or-later`
- AUR packaging license: `0BSD`
- Architectures: `x86_64`, `aarch64`

## Checks

- [x] `makepkg --verifysource -f` passed
- [x] `makepkg -f` built a local x86_64 package successfully
- [x] Local package installs to a test root and reports `nutsync 0.5.2`
- [x] PKGBUILD and generated `.SRCINFO` match
- [x] `bash -n packaging/arch/PKGBUILD` passed
- [x] Source tag exists and checksum matches
- [x] AUR package name `nutsync` is available (RPC result count 0)
- [x] GitHub release has both x86_64 and aarch64 packages
- [x] x86_64 package installs and reports `nutsync 0.5.2`
- [x] CI `AUR package (tag/manual)` passed, including `namcap`
- [x] Desktop file validation passed
- [x] AppStream validation passed
- [x] Local AUR repository is initialized, clean, and remote is configured

## Local AUR repository

```text
~/Projects/nutsync-aur
d725578 nutsync 0.5.2-1
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
