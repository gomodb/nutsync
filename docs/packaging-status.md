# Packaging and release status

## Current artifacts

- Application version: `0.6.0`
- GitHub release: <https://github.com/gomodb/nutsync/releases/tag/v0.6.0>
- x86_64 Arch package: `nutsync-0.6.0-1-x86_64.pkg.tar.zst`
- aarch64 Arch package: `nutsync-0.6.0-1-aarch64.pkg.tar.xz`
- Source archive SHA-256:
  `53f6171bfed358af4b0e677cf07108dbfa24f3964f27a7e0d8cca6c739d7cbf9`

Release checksums are published in the release's `SHA256SUMS` asset.

## Arch Linux packaging

The local package recipe is [packaging/arch/PKGBUILD](../packaging/arch/PKGBUILD).
It is used for local builds and for the GitHub Release packages.

The GitHub Actions `arch-package` job builds and smoke-tests the x86_64 package
on Arch Linux. The `arch-arm-package` job builds and smoke-tests the native
aarch64 package on an Arch Linux ARM container. Both packages are attached to a
`v*` release.

## Local build

```bash
cd packaging/arch
makepkg -f
```

The build directory is kept inside the extracted source tree so that a stale
CMake cache from an older version cannot break a new build.

