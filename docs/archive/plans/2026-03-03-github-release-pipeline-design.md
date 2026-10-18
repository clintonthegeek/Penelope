# GitHub Actions Release Pipeline Design

## Overview

Set up a GitHub Actions workflow that builds Penelope from source on Ubuntu 24.04,
packages it as both a `.deb` and an AppImage, and publishes both artifacts to a
GitHub Release.

## Trigger

- Tag push matching `v*` (e.g. `v0.1.0`)
- Version extracted from tag name for package metadata

## Build Environment

- Runner: `ubuntu-24.04`
- Dependencies installed via `apt-get`:
  - Qt6: `qt6-base-dev`, `qt6-base-dev-tools`, `libqt6svg6-dev`, `qt6-tools-dev`, `qt6-tools-dev-tools`
  - KDE: `extra-cmake-modules`, `libkf6coreaddons-dev`, `libkf6i18n-dev`, `libkf6xmlgui-dev`,
    `libkf6widgetsaddons-dev`, `libkf6iconthemes-dev`, `libkf6config-dev`, `libkf6configwidgets-dev`,
    `libkf6kio-dev`, `libkf6syntaxhighlighting-dev`, `libkf6dbusaddons-dev`
  - Libraries: `libmd4c-dev`, `libhyphen-dev`, `libpoppler-qt6-dev`, `libfreetype-dev`,
    `libicu-dev`, `zlib1g-dev`, `libfontconfig-dev`, `libharfbuzz-dev`
  - Tools: `cmake`, `g++`, `pkg-config`, `fuse`, `libfuse2`

## Build Steps

1. CMake configure with `-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release`
2. Build with `cmake --build`
3. Install to staging directory with `DESTDIR`

## .deb Packaging (CPack)

Add CPack configuration to the root `CMakeLists.txt`:

- Generator: DEB
- Package name: `penelope`
- Version: from `PROJECT_VERSION`
- Maintainer, description, homepage metadata
- Section: `text`
- `CPACK_DEBIAN_PACKAGE_DEPENDS` listing runtime library packages
- `CPACK_DEBIAN_FILE_NAME` set to `DEB-DEFAULT` for standard naming

The existing `install()` rules handle:
- Binary to `/usr/bin/penelope`
- XMLGUI rc file to KDE data dir
- Desktop file to applications dir (registers MIME types `text/markdown`, `text/x-markdown`, `text/plain`)

MIME registration for `.md` files works via the `MimeType=` line in the `.desktop` file;
`update-desktop-database` runs automatically via dpkg triggers on install.

## AppImage Packaging

- Download `linuxdeploy-x86_64.AppImage` and `linuxdeploy-plugin-qt-x86_64.AppImage`
- Run linuxdeploy against the DESTDIR install prefix
- Point it at the `.desktop` file and app icon
- Produces `Penelope-x86_64.AppImage`
- Fully self-contained: bundles all Qt6 and KF6 libraries

## App Icon

- Create a placeholder SVG icon at `icons/penelope.svg`
- Install it to the standard icon path via CMake `install()` rule
- Update `.desktop` file `Icon=` to reference `penelope` (without extension)
- The placeholder can be replaced with proper branding later

## GitHub Release

- Use `softprops/action-gh-release` action
- Creates release from the tag, auto-generates release notes
- Uploads `.deb` and `.AppImage` as release assets
- Asset names include version (e.g. `penelope_0.1.0_amd64.deb`, `Penelope-0.1.0-x86_64.AppImage`)

## Files to Create/Modify

1. **`icons/penelope.svg`** — new placeholder app icon
2. **`CMakeLists.txt`** — add icon install rule, CPack configuration
3. **`org.penelope.Penelope.desktop`** — update `Icon=penelope`
4. **`.github/workflows/release.yml`** — new release workflow
