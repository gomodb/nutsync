# Packaging and release status

## Current artifacts

- Application version: `0.4.7`
- GitHub release: <https://github.com/gomodb/nutsync/releases/tag/v0.4.7>
- AUR repository commit: `5748139 nutsync 0.4.7-1`
- x86_64 Arch package: `packaging/arch/nutsync-0.4.7-1-x86_64.pkg.tar.zst`
- x86_64 package SHA-256:
  `6d2b1af81d83a7422e87a4ae6963b6625751cb7554d732deadb46e4e40eb450d`

The main branch contains newer unreleased features. No version bump, tag, GitHub
release, or AUR update is performed automatically.

## AUR push blocker

`ssh -T aur@aur.archlinux.org` returns:

```text
Permission denied (publickey)
```

The following public key must be registered in the AUR account before the
prepared AUR repository can be pushed:

```text
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBpw077hEh6GPF6rv6Ar73HuRPLk2W5HdA9GGGVOSy4T fnoopv@arch
```

AUR package name: `nutsync`

Local AUR repository: `~/Projects/nutsync-aur` (branch `master`, package
`0.4.7-1`).

## Arch Linux ARM native package blocker

Native aarch64 packaging was not validated in this environment:

- local Docker daemon is inactive;
- no root/sudo access is available to start it;
- no qemu-user/proot fallback is installed;
- a GitHub Actions ARM64 Arch Linux container job was attempted, but the
  `pacman` dependency installation step failed;
- container job logs require repository admin access, so the failing step could
  not be inspected;
- the failing job was removed to keep main CI green.

The PKGBUILD already declares `aarch64`, and the Ubuntu arm64 CI job compiles and
tests the project. A real Arch Linux ARM `makepkg` run remains outstanding until
a working Arch Linux ARM environment is available.
