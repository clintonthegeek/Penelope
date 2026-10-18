# Packaging

## PKGBUILDs (Arch / Manjaro)

`generate-pkgbuild.sh` writes two AUR-style packages from the version CMake
defines:

| Directory | Package | Source |
|---|---|---|
| `out/penelope/` | `penelope` | pinned to the `v<version>` tag |
| `out/penelope-git/` | `penelope-git` | tracks `main`; version from `git describe` |

Run it with no arguments (it reads `project(Penelope VERSION ...)`), or pass a
version and output directory:

```sh
packaging/generate-pkgbuild.sh
packaging/generate-pkgbuild.sh 0.2.0 /tmp/penelope-aur
```

The release workflow runs the same script and attaches
`penelope-<version>-PKGBUILD` and `penelope-git-PKGBUILD` to the GitHub
release. Where `makepkg` is available, a `.SRCINFO` is generated beside each
PKGBUILD for uploading to the AUR.

Build and install locally:

```sh
cd packaging/out/penelope
makepkg -si
```

or for the VCS package:

```sh
cd packaging/out/penelope-git
makepkg -si
```

Do not edit the generated PKGBUILDs. Change the shared dependency list or the
`build()`/`package()` blocks in `generate-pkgbuild.sh`; both packages inherit
them, and the next release regenerates both.
