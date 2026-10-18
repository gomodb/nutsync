# Packaging and release status

## Current artifacts

- Application version: `0.5.2`
- GitHub release: <https://github.com/gomodb/nutsync/releases/tag/v0.5.2>
- AUR repository commit: `af09491 nutsync 0.5.2-1` (local, not pushed)
- x86_64 Arch package: `packaging/arch/nutsync-0.5.2-1-x86_64.pkg.tar.zst`
- x86_64 package SHA-256:
  `e14d2225bbb08ba1364914a039911ffceb5e1a44989bcf941ebca03260d17a62`
- Source archive SHA-256:
  `2e73cf4ab887f7fa9034504f9c6680b4c5c75bd7e60d324234e5ed64fcf38101`

NutSync 0.5.2 is released on GitHub. AUR publication remains blocked by the
missing AUR public-key registration.

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
`0.5.2-1`).

## Arch Linux ARM native package

Validated with the **Arch Linux ARM package** GitHub Actions workflow on the
native `ubuntu-24.04-arm` runner using the `menci/archlinuxarm:base-devel`
container.

Result:

- `makepkg` completed successfully;
- package installed with `pacman -U`;
- `nutsync --version` passed;
- desktop file and AppStream metadata were present;
- artifact uploaded as `nutsync-archlinuxarm`.

The package produced by that run is:

```text
nutsync-0.5.2-1-aarch64.pkg.tar.xz
```

Workflow: [.github/workflows/ci.yml](../.github/workflows/ci.yml) (`arch-arm-package` job).

The job runs on `v*` tags and its artifact is attached to the GitHub release.
