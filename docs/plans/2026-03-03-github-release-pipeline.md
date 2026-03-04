# GitHub Actions Release Pipeline Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create a GitHub Actions workflow that builds Penelope on Ubuntu 24.04, packages it as a `.deb` and AppImage, and publishes both to a GitHub Release on tag push.

**Architecture:** Single workflow triggered by `v*` tag push. Builds with CMake/Release, stages with DESTDIR, generates `.deb` via CPack and AppImage via linuxdeploy. Both artifacts uploaded to a GitHub Release.

**Tech Stack:** GitHub Actions, CMake/CPack, linuxdeploy + linuxdeploy-plugin-qt, Ubuntu 24.04

---

### Task 1: Create Placeholder App Icon

**Files:**
- Create: `icons/penelope.svg`

**Step 1: Create the icon directory and placeholder SVG**

Create `icons/penelope.svg` — a simple placeholder icon (a stylized document/pen icon).

```svg
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <!-- Page -->
  <rect x="48" y="24" width="160" height="208" rx="8" ry="8"
        fill="#f5f5f5" stroke="#3daee9" stroke-width="6"/>
  <!-- Text lines -->
  <line x1="80" y1="72" x2="176" y2="72" stroke="#888" stroke-width="4" stroke-linecap="round"/>
  <line x1="80" y1="96" x2="160" y2="96" stroke="#888" stroke-width="4" stroke-linecap="round"/>
  <line x1="80" y1="120" x2="172" y2="120" stroke="#888" stroke-width="4" stroke-linecap="round"/>
  <line x1="80" y1="144" x2="148" y2="144" stroke="#888" stroke-width="4" stroke-linecap="round"/>
  <line x1="80" y1="168" x2="168" y2="168" stroke="#888" stroke-width="4" stroke-linecap="round"/>
  <line x1="80" y1="192" x2="136" y2="192" stroke="#888" stroke-width="4" stroke-linecap="round"/>
  <!-- Pen nib overlay -->
  <g transform="translate(170,160) rotate(35)">
    <polygon points="0,-48 -8,12 0,4 8,12" fill="#3daee9"/>
    <polygon points="-8,12 0,24 8,12 0,4" fill="#1d6fa5"/>
  </g>
</svg>
```

**Step 2: Commit**

```bash
git add icons/penelope.svg
git commit -m "feat: add placeholder app icon for packaging"
```

---

### Task 2: Update Desktop File and CMakeLists.txt for Icon Install

**Files:**
- Modify: `org.penelope.Penelope.desktop` (line 8: Icon)
- Modify: `src/CMakeLists.txt` (after line 408: add icon install rule)

**Step 1: Update the desktop file Icon= line**

In `org.penelope.Penelope.desktop`, change:
```
Icon=document-viewer
```
to:
```
Icon=penelope
```

The icon name (without extension) must match the installed filename.

**Step 2: Add icon install rule to src/CMakeLists.txt**

After the existing `install(FILES ../org.penelope.Penelope.desktop ...)` block (around line 406-408), add:

```cmake
install(FILES ../icons/penelope.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
    RENAME penelope.svg
)
```

**Step 3: Commit**

```bash
git add org.penelope.Penelope.desktop src/CMakeLists.txt
git commit -m "feat: install app icon to hicolor, update desktop file"
```

---

### Task 3: Add CPack DEB Configuration

**Files:**
- Modify: `CMakeLists.txt` (append CPack block at end of file)

**Step 1: Add CPack configuration to the root CMakeLists.txt**

Append at the end of the root `CMakeLists.txt`:

```cmake
# ── Packaging (CPack) ─────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "penelope")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A beautiful paginated markdown reader")
set(CPACK_PACKAGE_DESCRIPTION "Penelope renders Markdown documents with typographic \
precision: custom page layouts, advanced hyphenation, PDF export with font subsetting, \
and rich theming support.")
set(CPACK_PACKAGE_CONTACT "clintonthegeek")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/clintonthegeek/Penelope")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Debian package
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SECTION "text")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)
```

Note: `CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON` uses `dpkg-shlibdeps` to auto-detect
runtime dependencies from linked shared libraries — much more reliable than
hand-listing them. This removes the need for a manual `CPACK_DEBIAN_PACKAGE_DEPENDS`.

**Step 2: Verify LICENSE file exists**

Check that a `LICENSE` file exists in the repo root. If not, we need to either create one or remove the `CPACK_RESOURCE_FILE_LICENSE` line.

**Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: add CPack DEB packaging configuration"
```

---

### Task 4: Create the GitHub Actions Release Workflow

**Files:**
- Create: `.github/workflows/release.yml`

**Step 1: Create the workflow directory**

```bash
mkdir -p .github/workflows
```

**Step 2: Write the release workflow**

Create `.github/workflows/release.yml`:

```yaml
name: Release

on:
  push:
    tags:
      - 'v*'

permissions:
  contents: write

jobs:
  build:
    runs-on: ubuntu-24.04

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Install KDE Neon repos (for KF6)
        run: |
          # KF6 dev packages may not be in stock Ubuntu 24.04 —
          # add KDE Neon stable repo as a fallback source.
          wget -qO- https://archive.neon.kde.org/public.key | sudo tee /etc/apt/trusted.gpg.d/kde-neon.asc
          echo "deb http://archive.neon.kde.org/user/noble noble main" | \
            sudo tee /etc/apt/sources.list.d/kde-neon.list
          # Pin KDE Neon lower so we only pull packages missing from Ubuntu
          cat <<'PIN' | sudo tee /etc/apt/preferences.d/kde-neon
          Package: *
          Pin: origin archive.neon.kde.org
          Pin-Priority: 100
          PIN

      - name: Install build dependencies
        run: |
          sudo apt-get update -qq
          sudo apt-get install -y --no-install-recommends \
            build-essential cmake ninja-build pkg-config dpkg-dev \
            qt6-base-dev qt6-base-dev-tools \
            extra-cmake-modules \
            libkf6coreaddons-dev libkf6i18n-dev libkf6xmlgui-dev \
            libkf6widgetsaddons-dev libkf6iconthemes-dev \
            libkf6config-dev libkf6configwidgets-dev \
            libkf6kio-dev libkf6syntaxhighlighting-dev \
            libkf6dbusaddons-dev \
            libmd4c-dev libhyphen-dev \
            libpoppler-qt6-dev \
            libfreetype-dev libicu-dev zlib1g-dev \
            libfontconfig-dev libharfbuzz-dev

      - name: Extract version from tag
        id: version
        run: echo "version=${GITHUB_REF_NAME#v}" >> "$GITHUB_OUTPUT"

      - name: Configure
        run: |
          cmake -B build -S . \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX=/usr \
            -G Ninja

      - name: Build
        run: cmake --build build --parallel

      # ── .deb via CPack ──────────────────────────────────────────────
      - name: Package .deb
        run: |
          cd build
          cpack -G DEB

      # ── AppImage via linuxdeploy ────────────────────────────────────
      - name: Install to AppDir
        run: DESTDIR=${{ github.workspace }}/AppDir cmake --install build

      - name: Download linuxdeploy
        run: |
          wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
          wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
          chmod +x linuxdeploy-*.AppImage

      - name: Build AppImage
        env:
          APPIMAGE_EXTRACT_AND_RUN: "1"
          NO_STRIP: "1"
          QMAKE: /usr/lib/qt6/bin/qmake
          VERSION: ${{ steps.version.outputs.version }}
        run: |
          ./linuxdeploy-x86_64.AppImage \
            --appdir AppDir \
            --plugin qt \
            --output appimage \
            --desktop-file AppDir/usr/share/applications/org.penelope.Penelope.desktop \
            --icon-file AppDir/usr/share/icons/hicolor/scalable/apps/penelope.svg

      # ── Upload release ──────────────────────────────────────────────
      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          generate_release_notes: true
          files: |
            build/penelope_*.deb
            Penelope-*.AppImage
```

**Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: add release workflow for .deb and AppImage"
```

---

### Task 5: Verify LICENSE File Exists

**Files:**
- Possibly create: `LICENSE`

**Step 1: Check if LICENSE exists**

```bash
ls -la LICENSE*
```

If it does not exist, either create one (the project likely uses GPL or similar given
KDE dependencies) or remove the `CPACK_RESOURCE_FILE_LICENSE` line from `CMakeLists.txt`.

**Step 2: Commit if needed**

```bash
git add LICENSE
git commit -m "docs: add license file"
```

---

### Task 6: Test by Pushing a Tag

**Step 1: Ensure all changes are committed and pushed**

```bash
git push origin main
```

**Step 2: Create and push a version tag**

```bash
git tag v0.1.0
git push origin v0.1.0
```

**Step 3: Monitor the Actions run**

```bash
gh run watch
```

If the build fails, iterate on the workflow:
- Check logs for missing packages
- Adjust KDE Neon repo setup if KF6 packages not found
- Fix linuxdeploy issues
- Adjust CPack configuration

**Step 4: Verify the release**

```bash
gh release view v0.1.0
```

Confirm both `.deb` and `.AppImage` are listed as assets.
