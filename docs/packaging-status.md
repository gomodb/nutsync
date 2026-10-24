# Packaging and release status

## Current artifacts

- Application version: `0.6.3`
- GitHub release: <https://github.com/gomodb/nutsync/releases/tag/v0.6.3>
- x86_64 Arch package: `nutsync-0.6.3-1-x86_64.pkg.tar.zst`
- aarch64 Arch package: `nutsync-0.6.3-1-aarch64.pkg.tar.xz`
- Source archive SHA-256:
  `63de1f16321815494360b589e5113da3ecf4d5a3962144b6641bc20f47571119`

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

