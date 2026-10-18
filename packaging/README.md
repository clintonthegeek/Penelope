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

## Releasing

`project(Penelope VERSION ...)` at the top of `CMakeLists.txt` is the single
source of truth. CMake passes it to the code as `PENELOPE_VERSION`, so the
About dialog and `penelope --version` show it, CPack names the `.deb` from it,
and `generate-pkgbuild.sh` writes it into both PKGBUILDs. The release workflow
reads the same line and refuses a tag whose name (without `v`) differs from it,
so the artifacts cannot drift apart.

1. Change the version in `CMakeLists.txt` and commit it (`release: 0.3`).
2. Tag `v<version>` and push the tag.
3. The workflow runs the tests, then attaches
   `penelope_<version>_<arch>.deb`, `Penelope-<version>-x86_64.AppImage`,
   `penelope-<version>-PKGBUILD` and `penelope-git-PKGBUILD` to the GitHub
   release.

Release notes are generated from the commits. To write them by hand, edit the
release after the workflow finishes (`gh release edit v<version> --notes-file …`).
