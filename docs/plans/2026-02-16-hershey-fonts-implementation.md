# Hershey Stroke Fonts Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add Hershey vector stroke fonts as a first-class rendering mode, enabling zero-font-embedding PDFs with 1960s-style stroked-path text.

**Architecture:** Per-theme `hersheyMode` flag routes font loading through a HersheyFontRegistry, bypasses HarfBuzz shaping, and renders text as stroked polyline paths in the PDF generator. System font fallback for missing characters uses existing FreeType outline decomposition. PDF Base 14 fonts handle the invisible text layer for markdown-copy mode.

**Tech Stack:** Qt6 C++20, JHF font format parsing, PDF path operators, existing FontManager/TextShaper/PdfGenerator pipeline.

**Design doc:** `docs/plans/2026-02-16-hershey-fonts-design.md`

---

### Task 1: Download and bundle Hershey font data

**Files:**
- Create: `src/hershey/` directory with 32 .jhf files
- Modify: `src/CMakeLists.txt:135-168` (add resource bundle)

**Step 1: Clone Hershey fonts and copy .jhf files**

```bash
cd /tmp
git clone --depth 1 https://github.com/kamalmostafa/hershey-fonts.git
mkdir -p /home/clinton/dev/Penelope/src/hershey
cp /tmp/hershey-fonts/hershey-fonts/*.jhf /home/clinton/dev/Penelope/src/hershey/
ls /home/clinton/dev/Penelope/src/hershey/*.jhf | wc -l
```

Expected: 32 (or close — verify exact count from the repo).

**Step 2: Add Qt resource bundle to CMakeLists.txt**

In `src/CMakeLists.txt`, after the `glyphs` resource block (line 168), add:

```cmake
# Bundle Hershey vector font data as Qt resources
qt_add_resources(PenelopeCore "hershey"
    PREFIX "/hershey"
    BASE hershey
    FILES
        hershey/astrology.jhf
        hershey/cursive.jhf
        hershey/cyrilc_1.jhf
        hershey/cyrillic.jhf
        hershey/futural.jhf
        hershey/futuram.jhf
        hershey/gothgbt.jhf
        hershey/gothgrt.jhf
        hershey/gothiceng.jhf
        hershey/gothicger.jhf
        hershey/gothicita.jhf
        hershey/gothitt.jhf
        hershey/greek.jhf
        hershey/greekc.jhf
        hershey/greeks.jhf
        hershey/japanese.jhf
        hershey/markers.jhf
        hershey/mathlow.jhf
        hershey/mathupp.jhf
        hershey/meteorology.jhf
        hershey/music.jhf
        hershey/rowmand.jhf
        hershey/rowmans.jhf
        hershey/rowmant.jhf
        hershey/scriptc.jhf
        hershey/scripts.jhf
        hershey/symbolic.jhf
        hershey/timesg.jhf
        hershey/timesi.jhf
        hershey/timesib.jhf
        hershey/timesr.jhf
        hershey/timesrb.jhf
)
```

**Step 3: Verify build still works**

Run: `cmake --build build --parallel`
Expected: Clean build. The resources compile but aren't used yet.

**Step 4: Commit**

```bash
git add src/hershey/ src/CMakeLists.txt
git commit -m "feat: bundle Hershey vector font data as Qt resources"
```

---

### Task 2: JHF parser and HersheyFont class

**Files:**
- Create: `src/font/hersheyfont.h`
- Create: `src/font/hersheyfont.cpp`
- Modify: `src/CMakeLists.txt:4-106` (add source files)

**Step 1: Create `src/font/hersheyfont.h`**

This header defines `HersheyGlyph`, `HersheyFont` (loads one .jhf file), and `HersheyFontRegistry` (maps family/weight/italic to concrete fonts).

```cpp
/*
 * hersheyfont.h — Hershey vector stroke font loading and registry
 *
 * Parses .jhf (James Hurt Format) files into polyline stroke data.
 * Provides font metrics for the layout engine and stroke data for
 * the PDF generator.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_HERSHEYFONT_H
#define PENELOPE_HERSHEYFONT_H

#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

struct HersheyGlyph {
    int leftBound = 0;
    int rightBound = 0;
    QVector<QVector<QPointF>> strokes; // list of polylines (pen-down segments)
};

class HersheyFont {
public:
    HersheyFont() = default;

    bool load(const QString &resourcePath);

    // Glyph access
    const HersheyGlyph *glyph(char32_t codepoint) const;
    bool hasGlyph(char32_t codepoint) const;

    // Metrics (in Hershey coordinate units — caller scales to points)
    int advanceWidth(char32_t codepoint) const;
    int ascent() const { return m_ascent; }
    int descent() const { return m_descent; }
    int unitsPerEm() const { return m_unitsPerEm; }

    const QString &name() const { return m_name; }

private:
    void computeMetrics();

    QString m_name;
    QHash<char32_t, HersheyGlyph> m_glyphs;
    int m_ascent = 0;   // positive, above baseline
    int m_descent = 0;  // positive, below baseline
    int m_unitsPerEm = 0;
};

// --- Font Registry ---

struct HersheyFontResult {
    HersheyFont *font = nullptr;
    bool synthesizeBold = false;
    bool synthesizeItalic = false;
};

class HersheyFontRegistry {
public:
    static HersheyFontRegistry &instance();

    // Ensure all fonts are loaded (lazy, idempotent)
    void ensureLoaded();

    // Resolve family + weight + italic to a concrete font + synthesis flags
    HersheyFontResult resolve(const QString &family, int weight, bool italic) const;

    // List available Hershey family names (for font selector)
    QStringList familyNames() const;

    // Direct font access (by .jhf filename without extension)
    HersheyFont *fontByName(const QString &name) const;

private:
    HersheyFontRegistry() = default;

    struct FamilyEntry {
        QString normal;       // .jhf name for regular weight
        QString bold;         // .jhf name for bold (empty = synthesize)
        QString italic;       // .jhf name for italic (empty = synthesize)
        QString boldItalic;   // .jhf name for bold-italic (empty = synthesize)
    };

    void loadAllFonts();
    void registerFamilies();

    QHash<QString, HersheyFont *> m_fonts;  // keyed by jhf name (e.g. "futural")
    QHash<QString, FamilyEntry> m_families;  // keyed by display name (e.g. "Hershey Sans")
    bool m_loaded = false;
};

#endif // PENELOPE_HERSHEYFONT_H
```

**Step 2: Create `src/font/hersheyfont.cpp`**

The JHF parser implementation. Key format details:
- Each glyph line: positions 0-4 = ID, 5-7 = vertex count, 8 = left bound char, 9 = right bound char, 10+ = coordinate pairs
- Coordinates: `char - 'R'` (ASCII 82) gives signed integer
- Pen-up: the two-char sequence ` R` (space + 'R')
- Lines wrap at 72 chars; continuation lines start with whitespace (no 5-char ID)

```cpp
/*
 * hersheyfont.cpp — JHF parser and Hershey font registry
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hersheyfont.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>

// --- HersheyFont ---

bool HersheyFont::load(const QString &resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    m_name = QFileInfo(resourcePath).baseName();

    // JHF files encode one glyph per logical line. Physical lines wrap at 72 chars.
    // ASCII printable characters start at codepoint 32 (space).
    char32_t codepoint = 32;

    QByteArray accumulated;

    while (!file.atEnd()) {
        QByteArray rawLine = file.readLine();
        // Strip trailing newline
        while (rawLine.endsWith('\n') || rawLine.endsWith('\r'))
            rawLine.chop(1);

        if (rawLine.isEmpty())
            continue;

        // Continuation line: starts with spaces (no 5-digit glyph number)
        // A new glyph line starts with a digit in columns 0-4
        bool isContinuation = (rawLine.size() >= 5 && rawLine[0] == ' '
                               && (rawLine.size() < 6 || !rawLine.mid(0, 5).trimmed().isEmpty() == false));

        // More robust: a new glyph starts when columns 0-4 contain a number
        QByteArray idPart = rawLine.left(5).trimmed();
        bool isNewGlyph = !idPart.isEmpty() && idPart[0] >= '0' && idPart[0] <= '9';

        if (isNewGlyph && !accumulated.isEmpty()) {
            // Parse the previous accumulated glyph
            parseGlyphLine(accumulated, codepoint);
            codepoint++;
            accumulated.clear();
        }

        accumulated += rawLine;
    }

    // Parse last glyph
    if (!accumulated.isEmpty()) {
        parseGlyphLine(accumulated, codepoint);
    }

    computeMetrics();
    return !m_glyphs.isEmpty();
}

void HersheyFont::parseGlyphLine(const QByteArray &line, char32_t codepoint)
{
    if (line.size() < 10)
        return;

    // Positions 5-7: vertex count (3-char integer)
    int vertexCount = line.mid(5, 3).trimmed().toInt();
    if (vertexCount < 1)
        return;

    // Position 8-9: left and right boundaries
    int leftBound = static_cast<int>(line[8]) - static_cast<int>('R');
    int rightBound = static_cast<int>(line[9]) - static_cast<int>('R');

    HersheyGlyph glyph;
    glyph.leftBound = leftBound;
    glyph.rightBound = rightBound;

    // Positions 10+: coordinate pairs (each coordinate = one char)
    // vertexCount includes the left/right boundary pair, so actual vertices = vertexCount - 1
    QVector<QPointF> currentStroke;

    int i = 10;
    int pairsRead = 0;
    int maxPairs = vertexCount - 1; // boundary pair counts as 1

    while (i + 1 < line.size() && pairsRead < maxPairs) {
        char cx = line[i];
        char cy = line[i + 1];

        // Pen-up marker: ' R' (space followed by 'R')
        if (cx == ' ' && cy == 'R') {
            if (!currentStroke.isEmpty()) {
                glyph.strokes.append(currentStroke);
                currentStroke.clear();
            }
            i += 2;
            pairsRead++;
            continue;
        }

        qreal x = static_cast<int>(cx) - static_cast<int>('R');
        qreal y = static_cast<int>(cy) - static_cast<int>('R');

        // Hershey Y-axis is inverted (positive = down), flip for our coordinate system
        currentStroke.append(QPointF(x, -y));

        i += 2;
        pairsRead++;
    }

    if (!currentStroke.isEmpty())
        glyph.strokes.append(currentStroke);

    m_glyphs.insert(codepoint, glyph);
}

const HersheyGlyph *HersheyFont::glyph(char32_t codepoint) const
{
    auto it = m_glyphs.constFind(codepoint);
    return (it != m_glyphs.constEnd()) ? &it.value() : nullptr;
}

bool HersheyFont::hasGlyph(char32_t codepoint) const
{
    return m_glyphs.contains(codepoint);
}

int HersheyFont::advanceWidth(char32_t codepoint) const
{
    if (auto *g = glyph(codepoint))
        return g->rightBound - g->leftBound;
    return 0;
}

void HersheyFont::computeMetrics()
{
    // Scan all glyphs to find bounding box extremes
    qreal minY = 0, maxY = 0;
    for (auto it = m_glyphs.constBegin(); it != m_glyphs.constEnd(); ++it) {
        for (const auto &stroke : it.value().strokes) {
            for (const QPointF &pt : stroke) {
                if (pt.y() > maxY) maxY = pt.y();
                if (pt.y() < minY) minY = pt.y();
            }
        }
    }

    m_ascent = static_cast<int>(std::ceil(maxY));   // above baseline (positive)
    m_descent = static_cast<int>(std::ceil(-minY));  // below baseline (positive)
    m_unitsPerEm = m_ascent + m_descent;
    if (m_unitsPerEm < 1)
        m_unitsPerEm = 1;
}

// --- HersheyFontRegistry ---

HersheyFontRegistry &HersheyFontRegistry::instance()
{
    static HersheyFontRegistry reg;
    return reg;
}

void HersheyFontRegistry::ensureLoaded()
{
    if (m_loaded)
        return;
    loadAllFonts();
    registerFamilies();
    m_loaded = true;
}

void HersheyFontRegistry::loadAllFonts()
{
    static const char *fontNames[] = {
        "astrology", "cursive", "cyrilc_1", "cyrillic",
        "futural", "futuram",
        "gothgbt", "gothgrt", "gothiceng", "gothicger", "gothicita", "gothitt",
        "greek", "greekc", "greeks",
        "japanese", "markers", "mathlow", "mathupp", "meteorology", "music",
        "rowmand", "rowmans", "rowmant",
        "scriptc", "scripts",
        "symbolic",
        "timesg", "timesi", "timesib", "timesr", "timesrb",
        nullptr
    };

    for (int i = 0; fontNames[i]; ++i) {
        QString name = QString::fromLatin1(fontNames[i]);
        QString path = QStringLiteral(":/hershey/%1.jhf").arg(name);
        auto *font = new HersheyFont;
        if (font->load(path)) {
            m_fonts.insert(name, font);
        } else {
            qWarning() << "HersheyFontRegistry: failed to load" << path;
            delete font;
        }
    }
}

void HersheyFontRegistry::registerFamilies()
{
    // Family name → { normal, bold, italic, boldItalic }
    // Empty string means "synthesize from normal variant"
    m_families = {
        {QStringLiteral("Hershey Sans"),
         {QStringLiteral("futural"), QStringLiteral("futuram"), {}, {}}},
        {QStringLiteral("Hershey Roman"),
         {QStringLiteral("rowmans"), QStringLiteral("rowmant"), {}, {}}},
        {QStringLiteral("Hershey Serif"),
         {QStringLiteral("timesr"), QStringLiteral("timesrb"),
          QStringLiteral("timesi"), QStringLiteral("timesib")}},
        {QStringLiteral("Hershey Script"),
         {QStringLiteral("scripts"), QStringLiteral("scriptc"), {}, {}}},
        {QStringLiteral("Hershey Gothic English"),
         {QStringLiteral("gothiceng"), {}, {}, {}}},
        {QStringLiteral("Hershey Gothic German"),
         {QStringLiteral("gothicger"), QStringLiteral("gothgbt"), {}, {}}},
        {QStringLiteral("Hershey Gothic Italian"),
         {QStringLiteral("gothicita"), QStringLiteral("gothitt"), {}, {}}},
        {QStringLiteral("Hershey Greek"),
         {QStringLiteral("greek"), QStringLiteral("greekc"),
          QStringLiteral("greeks"), {}}},
        {QStringLiteral("Hershey Cyrillic"),
         {QStringLiteral("cyrillic"), QStringLiteral("cyrilc_1"), {}, {}}},
    };
}

HersheyFontResult HersheyFontRegistry::resolve(const QString &family, int weight, bool italic) const
{
    HersheyFontResult result;

    auto famIt = m_families.constFind(family);
    if (famIt == m_families.constEnd())
        return result;

    const FamilyEntry &entry = famIt.value();
    bool wantBold = (weight >= 600); // QFont::DemiBold and above

    // Pick the best native variant
    QString fontName;
    if (wantBold && italic) {
        fontName = entry.boldItalic;
        if (fontName.isEmpty()) {
            // Try bold + synthesize italic
            fontName = entry.bold.isEmpty() ? entry.normal : entry.bold;
            result.synthesizeItalic = true;
            result.synthesizeBold = entry.bold.isEmpty();
        }
    } else if (wantBold) {
        fontName = entry.bold;
        if (fontName.isEmpty()) {
            fontName = entry.normal;
            result.synthesizeBold = true;
        }
    } else if (italic) {
        fontName = entry.italic;
        if (fontName.isEmpty()) {
            fontName = entry.normal;
            result.synthesizeItalic = true;
        }
    } else {
        fontName = entry.normal;
    }

    auto fontIt = m_fonts.constFind(fontName);
    if (fontIt != m_fonts.constEnd())
        result.font = fontIt.value();

    return result;
}

QStringList HersheyFontRegistry::familyNames() const
{
    QStringList names = m_families.keys();
    names.sort();
    return names;
}

HersheyFont *HersheyFontRegistry::fontByName(const QString &name) const
{
    return m_fonts.value(name, nullptr);
}
```

Note: The `parseGlyphLine` method needs to be declared in the header as a private method. Add it to the HersheyFont class:

```cpp
// In hersheyfont.h, add to HersheyFont private section:
    void parseGlyphLine(const QByteArray &line, char32_t codepoint);
```

**Step 3: Add source files to CMakeLists.txt**

In `src/CMakeLists.txt`, after `font/sfnt.h` (line 70), add:

```cmake
    font/hersheyfont.cpp
    font/hersheyfont.h
```

**Step 4: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build. New files compile but aren't called yet.

**Step 5: Commit**

```bash
git add src/font/hersheyfont.h src/font/hersheyfont.cpp src/CMakeLists.txt
git commit -m "feat: add HersheyFont JHF parser and font registry"
```

---

### Task 3: FontFace `isHershey` flag and FontManager routing

**Files:**
- Modify: `src/font/fontmanager.h:39-49` (add isHershey flag + hersheyFont pointer to FontFace)
- Modify: `src/font/fontmanager.cpp:110-126` (route Hershey families in loadFont)
- Modify: `src/font/fontmanager.cpp` (metrics: ascent, descent, glyphWidth delegate to Hershey)

**Step 1: Extend FontFace struct**

In `src/font/fontmanager.h`, add to the `FontFace` struct (after line 46):

```cpp
    // Hershey stroke font support
    bool isHershey = false;
    class HersheyFont *hersheyFont = nullptr;
    bool hersheyBold = false;    // synthesize bold via stroke width
    bool hersheyItalic = false;  // synthesize italic via skew
```

Also add `#include "hersheyfont.h"` at the top of fontmanager.h (or forward-declare `class HersheyFont;`).

**Step 2: Route Hershey families in loadFont()**

In `src/font/fontmanager.cpp`, modify `loadFont()` (around line 110). Before the fontconfig resolution, add a Hershey check:

```cpp
FontFace *FontManager::loadFont(const QString &family, int weight, bool italic)
{
    FontKey key{family, weight, italic};
    if (auto *existing = m_faces.value(key))
        return existing;

    // --- Hershey font routing ---
    if (family.startsWith(QLatin1String("Hershey "))) {
        HersheyFontRegistry::instance().ensureLoaded();
        HersheyFontResult hr = HersheyFontRegistry::instance().resolve(family, weight, italic);
        if (hr.font) {
            auto *face = new FontFace;
            face->isHershey = true;
            face->hersheyFont = hr.font;
            face->hersheyBold = hr.synthesizeBold;
            face->hersheyItalic = hr.synthesizeItalic;
            m_faces.insert(key, face);
            return face;
        }
        qWarning() << "FontManager: Could not resolve Hershey font:" << family << weight << italic;
        return nullptr;
    }

    // --- Regular font path (fontconfig) ---
    QString path = resolveFontPath(family, weight, italic);
    // ... rest unchanged ...
```

**Step 3: Add Hershey metrics delegation**

In `src/font/fontmanager.cpp`, modify the metric methods to check `isHershey`:

```cpp
qreal FontManager::ascent(FontFace *face, qreal sizePoints) const
{
    if (!face) return 0;
    if (face->isHershey && face->hersheyFont) {
        qreal scale = sizePoints / face->hersheyFont->unitsPerEm();
        return face->hersheyFont->ascent() * scale;
    }
    // ... existing FreeType path ...
}

qreal FontManager::descent(FontFace *face, qreal sizePoints) const
{
    if (!face) return 0;
    if (face->isHershey && face->hersheyFont) {
        qreal scale = sizePoints / face->hersheyFont->unitsPerEm();
        return face->hersheyFont->descent() * scale;
    }
    // ... existing FreeType path ...
}

qreal FontManager::lineHeight(FontFace *face, qreal sizePoints) const
{
    if (!face) return 0;
    if (face->isHershey && face->hersheyFont) {
        return ascent(face, sizePoints) + descent(face, sizePoints);
    }
    // ... existing FreeType path ...
}

qreal FontManager::glyphWidth(FontFace *face, uint glyphId, qreal sizePoints) const
{
    if (!face) return 0;
    if (face->isHershey && face->hersheyFont) {
        // For Hershey, glyphId == codepoint
        qreal scale = sizePoints / face->hersheyFont->unitsPerEm();
        return face->hersheyFont->advanceWidth(static_cast<char32_t>(glyphId)) * scale;
    }
    // ... existing FreeType path ...
}
```

**Step 4: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 5: Commit**

```bash
git add src/font/fontmanager.h src/font/fontmanager.cpp
git commit -m "feat: route Hershey font families through FontManager with metrics"
```

---

### Task 4: TextShaper bypass for Hershey fonts

**Files:**
- Modify: `src/font/textshaper.cpp:227-322` (shape method)
- Modify: `src/font/textshaper.cpp:145-223` (font coverage)

**Step 1: Add Hershey shaping bypass in shape()**

In `src/font/textshaper.cpp`, inside the `shape()` method's main loop (around line 239), after loading the font face, add a Hershey branch that bypasses HarfBuzz:

```cpp
    for (const InternalRun &run : textRuns) {
        if (run.styleIndex < 0 || run.styleIndex >= styles.size())
            continue;
        const StyleRun &style = styles[run.styleIndex];

        // Load font (use fallback if coverage itemization flagged this run)
        FontFace *face;
        if (run.useFallbackFont && m_fallbackFont) {
            face = m_fallbackFont;
        } else {
            face = m_fontManager->loadFont(
                style.fontFamily, style.fontWeight, style.fontItalic);
        }
        if (!face)
            continue;

        // --- Hershey font: simple 1:1 character→glyph mapping ---
        if (face->isHershey && face->hersheyFont) {
            ShapedRun shaped;
            shaped.font = face;
            shaped.fontSize = style.fontSize;
            shaped.textStart = run.start;
            shaped.textLength = run.length;
            shaped.rtl = false; // Hershey fonts are LTR only

            qreal scale = style.fontSize / face->hersheyFont->unitsPerEm();

            for (int ci = run.start; ci < run.start + run.length; ++ci) {
                char32_t cp = text.at(ci).unicode();
                // Handle surrogate pairs
                if (ci + 1 < run.start + run.length
                    && QChar::isHighSurrogate(text.at(ci).unicode())
                    && QChar::isLowSurrogate(text.at(ci + 1).unicode())) {
                    cp = QChar::surrogateToUcs4(text.at(ci), text.at(ci + 1));
                }

                ShapedGlyph g;
                g.glyphId = static_cast<uint>(cp); // glyphId == codepoint
                g.xAdvance = face->hersheyFont->advanceWidth(cp) * scale;
                g.yAdvance = 0;
                g.xOffset = 0;
                g.yOffset = 0;
                g.cluster = ci;
                shaped.glyphs.append(g);
            }

            result.append(shaped);
            continue; // skip HarfBuzz path
        }

        // --- Existing HarfBuzz path ---
        if (!face->hbFont)
            continue;
        // ... rest of existing code unchanged ...
```

**Step 2: Add Hershey font coverage check**

In `itemizeFontCoverage()` (line 145), the primary font's FreeType face is used for `FT_Get_Char_Index`. For Hershey fonts, use `hasGlyph()` instead. Modify the coverage check (around line 161):

```cpp
        FontFace *primary = m_fontManager->loadFont(
            style.fontFamily, style.fontWeight, style.fontItalic);
        if (!primary) {
            result.append(run);
            continue;
        }

        // Hershey fonts use their own glyph lookup, not FreeType
        if (primary->isHershey && primary->hersheyFont) {
            // Split run at Hershey coverage boundaries
            int pos = run.start;
            int end = run.start + run.length;
            while (pos < end) {
                char32_t cp = text.at(pos).unicode();
                bool primaryHas = primary->hersheyFont->hasGlyph(cp);
                bool useFallback = !primaryHas
                    && m_fallbackFont && m_fallbackFont->ftFace
                    && FT_Get_Char_Index(m_fallbackFont->ftFace, cp) != 0;

                int segStart = pos;
                pos++;

                while (pos < end) {
                    char32_t cp2 = text.at(pos).unicode();
                    bool p2 = primary->hersheyFont->hasGlyph(cp2);
                    bool f2 = !p2 && m_fallbackFont && m_fallbackFont->ftFace
                        && FT_Get_Char_Index(m_fallbackFont->ftFace, cp2) != 0;
                    if (f2 != useFallback)
                        break;
                    pos++;
                }

                InternalRun sub;
                sub.start = segStart;
                sub.length = pos - segStart;
                sub.dir = run.dir;
                sub.script = run.script;
                sub.styleIndex = run.styleIndex;
                sub.useFallbackFont = useFallback;
                result.append(sub);
            }
            continue;
        }

        // Existing FreeType coverage path
        if (!primary->ftFace) {
            result.append(run);
            continue;
        }
        // ... rest unchanged ...
```

You'll need to add `#include "hersheyfont.h"` at the top of textshaper.cpp (or it may already be included transitively via fontmanager.h).

**Step 3: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 4: Commit**

```bash
git add src/font/textshaper.cpp
git commit -m "feat: bypass HarfBuzz for Hershey fonts with direct character mapping"
```

---

### Task 5: Theme and style integration (hersheyMode flag)

**Files:**
- Modify: `src/style/stylemanager.h:57-61` (add hersheyMode flag)
- Modify: `src/style/thememanager.cpp:131-180` (parse hersheyMode from JSON)
- Modify: `src/style/thememanager.cpp` (serialize hersheyMode)

**Step 1: Add hersheyMode to StyleManager**

In `src/style/stylemanager.h`, add a `hersheyMode` flag. After the `FootnoteStyle m_footnoteStyle;` line (line 61), add:

```cpp
    bool m_hersheyMode = false;
```

And add public accessors (near line 49, after the footnoteStyle methods):

```cpp
    bool hersheyMode() const { return m_hersheyMode; }
    void setHersheyMode(bool enabled) { m_hersheyMode = enabled; }
```

**Step 2: Parse hersheyMode in ThemeManager**

In `src/style/thememanager.cpp`, in `loadThemeFromJson()`, after parsing `QJsonObject root = doc.object();` (line 141), add:

```cpp
    // Hershey mode
    sm->setHersheyMode(root.value(QLatin1String("hersheyMode")).toBool(false));
```

**Step 3: Serialize hersheyMode in theme save**

Find the `serializeTheme()` method in thememanager.cpp. In the root object construction, add:

```cpp
    if (sm->hersheyMode())
        root[QLatin1String("hersheyMode")] = true;
```

**Step 4: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 5: Commit**

```bash
git add src/style/stylemanager.h src/style/thememanager.cpp
git commit -m "feat: add hersheyMode flag to StyleManager and theme JSON parsing"
```

---

### Task 6: PDF Generator — renderHersheyGlyphBox

**Files:**
- Modify: `src/pdf/pdfgenerator.h:53-61` (add method declaration)
- Modify: `src/pdf/pdfgenerator.cpp` (add renderHersheyGlyphBox implementation)

**Step 1: Declare renderHersheyGlyphBox in header**

In `src/pdf/pdfgenerator.h`, after the `renderGlyphBoxAsPath` declaration (line 56), add:

```cpp
    void renderHersheyGlyphBox(const Layout::GlyphBox &gbox, QByteArray &stream,
                               qreal x, qreal y);
```

Also add `#include "hersheyfont.h"` at the top.

**Step 2: Implement renderHersheyGlyphBox**

In `src/pdf/pdfgenerator.cpp`, add the new method (after `renderGlyphBoxAsPath` which ends at line 1002):

```cpp
void PdfGenerator::renderHersheyGlyphBox(const Layout::GlyphBox &gbox,
                                          QByteArray &stream,
                                          qreal x, qreal y)
{
    if (gbox.glyphs.isEmpty() || !gbox.font || !gbox.font->isHershey
        || !gbox.font->hersheyFont)
        return;

    const HersheyFont *hfont = gbox.font->hersheyFont;

    // Background (inline code highlight)
    if (gbox.style.background.isValid()) {
        stream += "q\n";
        stream += colorOperator(gbox.style.background, true);
        stream += pdfCoord(x - 1) + " " + pdfCoord(y - gbox.descent - 1) + " "
                + pdfCoord(gbox.width + 2) + " " + pdfCoord(gbox.ascent + gbox.descent + 2)
                + " re f\n";
        stream += "Q\n";
    }

    qreal scale = gbox.fontSize / hfont->unitsPerEm();

    // Stroke styling
    qreal baseStrokeWidth = 0.02 * gbox.fontSize;
    qreal strokeWidth = baseStrokeWidth;
    if (gbox.font->hersheyBold)
        strokeWidth *= 1.8;

    stream += "q\n";
    stream += colorOperator(gbox.style.foreground, false); // stroke color
    stream += pdfCoord(strokeWidth) + " w\n";
    stream += "1 J\n"; // round line cap
    stream += "1 j\n"; // round line join

    qreal curX = x;
    for (const auto &g : gbox.glyphs) {
        qreal gx = curX + g.xOffset;
        qreal gy = y - g.yOffset;

        // Superscript/subscript
        if (gbox.style.superscript)
            gy += gbox.fontSize * 0.35;
        else if (gbox.style.subscript)
            gy -= gbox.fontSize * 0.15;

        const HersheyGlyph *hg = hfont->glyph(static_cast<char32_t>(g.glyphId));
        if (hg) {
            // Apply italic skew if synthesized
            bool needSkew = gbox.font->hersheyItalic;
            if (needSkew) {
                // Save state and apply skew transform
                // tan(12°) ≈ 0.2126
                stream += "q\n";
                stream += "1 0 0.2126 1 " + pdfCoord(gx) + " " + pdfCoord(gy) + " cm\n";
            }

            for (const auto &stroke : hg->strokes) {
                if (stroke.size() < 2)
                    continue;
                for (int si = 0; si < stroke.size(); ++si) {
                    // Translate glyph: shift by -leftBound so glyph starts at origin,
                    // then scale and position
                    qreal px = (stroke[si].x() - hg->leftBound) * scale;
                    qreal py = stroke[si].y() * scale;

                    if (needSkew) {
                        // Coordinates are relative to the cm transform origin
                        px -= gx; // undo the cm translate for relative positioning...
                        // Actually, with cm active, emit relative to (0,0)
                    }

                    qreal finalX = needSkew ? px : (gx + px);
                    qreal finalY = needSkew ? py : (gy + py);

                    if (si == 0)
                        stream += pdfCoord(finalX) + " " + pdfCoord(finalY) + " m\n";
                    else
                        stream += pdfCoord(finalX) + " " + pdfCoord(finalY) + " l\n";
                }
                stream += "S\n"; // stroke the path
            }

            if (needSkew)
                stream += "Q\n";
        }

        curX += g.xAdvance;
    }

    stream += "Q\n"; // restore stroke styling state

    // Underline
    if (gbox.style.underline) {
        stream += "q\n";
        stream += colorOperator(gbox.style.foreground, false);
        stream += "0.5 w\n";
        qreal uy = y - gbox.descent * 0.3;
        stream += pdfCoord(x) + " " + pdfCoord(uy) + " m "
                + pdfCoord(curX) + " " + pdfCoord(uy) + " l S\n";
        stream += "Q\n";
    }

    // Strikethrough
    if (gbox.style.strikethrough) {
        stream += "q\n";
        stream += colorOperator(gbox.style.foreground, false);
        stream += "0.5 w\n";
        qreal sy = y + gbox.ascent * 0.3;
        stream += pdfCoord(x) + " " + pdfCoord(sy) + " m "
                + pdfCoord(curX) + " " + pdfCoord(sy) + " l S\n";
        stream += "Q\n";
    }

    // Link annotation
    if (!gbox.style.linkHref.isEmpty()) {
        collectLinkRect(x, y, curX - x, gbox.ascent, gbox.descent,
                        gbox.style.linkHref);
    }
}
```

**Important note:** The italic skew transform interaction with absolute positioning needs careful handling. The simplest correct approach:
- When `needSkew` is true, use `cm` to set origin at the glyph's baseline position, then emit all coordinates relative to (0, 0). The skew matrix automatically tilts the strokes.
- When `needSkew` is false, emit absolute coordinates (gx + px, gy + py).

Simplify the skew logic in the implementation to:

```cpp
            if (needSkew) {
                stream += "q\n";
                stream += "1 0 0.2126 1 " + pdfCoord(gx) + " " + pdfCoord(gy) + " cm\n";
            }

            for (const auto &stroke : hg->strokes) {
                if (stroke.size() < 2)
                    continue;
                for (int si = 0; si < stroke.size(); ++si) {
                    qreal px = (stroke[si].x() - hg->leftBound) * scale;
                    qreal py = stroke[si].y() * scale;

                    // When skewed, coordinates are relative to cm origin
                    // When not skewed, coordinates are absolute
                    qreal finalX = needSkew ? px : (gx + px);
                    qreal finalY = needSkew ? py : (gy + py);

                    if (si == 0)
                        stream += pdfCoord(finalX) + " " + pdfCoord(finalY) + " m\n";
                    else
                        stream += pdfCoord(finalX) + " " + pdfCoord(finalY) + " l\n";
                }
                stream += "S\n";
            }

            if (needSkew)
                stream += "Q\n";
```

**Step 3: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build. Method exists but isn't called yet.

**Step 4: Commit**

```bash
git add src/pdf/pdfgenerator.h src/pdf/pdfgenerator.cpp
git commit -m "feat: add renderHersheyGlyphBox for stroked-path text rendering"
```

---

### Task 7: Wire Hershey rendering into renderLineBox

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp:540-735` (renderLineBox)

**Step 1: Route to Hershey renderer in non-markdown mode**

In `renderLineBox()`, the non-markdown branches (lines 706-735) call `renderGlyphBox()`. Add Hershey detection. Replace the `renderGlyphBox` calls:

In the justified branch (line 709):
```cpp
            if (line.glyphs[i].font && line.glyphs[i].font->isHershey)
                renderHersheyGlyphBox(line.glyphs[i], stream, x, baselineY);
            else
                renderGlyphBox(line.glyphs[i], stream, x, baselineY);
```

In the non-justified branch (line 732):
```cpp
            if (gbox.font && gbox.font->isHershey)
                renderHersheyGlyphBox(gbox, stream, x, baselineY);
            else
                renderGlyphBox(gbox, stream, x, baselineY);
```

**Step 2: Route to Hershey renderer in markdown mode**

In the markdown mode path rendering (line 699-700), the visible text is rendered as paths. For Hershey fonts, use `renderHersheyGlyphBox` instead of `renderGlyphBoxAsPath`:

```cpp
        // Path rendering for visible text
        for (int i = 0; i < line.glyphs.size(); ++i) {
            if (line.glyphs[i].font && line.glyphs[i].font->isHershey)
                renderHersheyGlyphBox(line.glyphs[i], stream, glyphXPositions[i], baselineY);
            else
                renderGlyphBoxAsPath(line.glyphs[i], stream, glyphXPositions[i], baselineY);
        }
```

**Step 3: Handle invisible text overlay for Hershey fonts in markdown mode**

In the markdown mode invisible text overlay (lines 675-693), the code references `pdfFontName(gbox.font)` which requires a registered embedded font. For Hershey fonts, we need to use a Base 14 font instead.

This needs careful handling. The invisible text layer needs *some* font to emit Tj operators. For Hershey glyph boxes, use a Base 14 font reference. Add a helper in PdfGenerator:

In `src/pdf/pdfgenerator.h`, add a member:
```cpp
    bool m_hersheyMode = false;
    QByteArray m_base14FontName; // e.g. "Helvetica" for invisible text in Hershey mode
```

In `src/pdf/pdfgenerator.cpp`, in the `generate()` method, detect if any font is Hershey and set the flag:
```cpp
    // Detect Hershey mode
    m_hersheyMode = false;
    for (const auto &page : layout.pages) {
        for (const auto &elem : page.elements) {
            std::visit([&](const auto &e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, Layout::BlockBox>) {
                    for (const auto &line : e.lines)
                        for (const auto &gbox : line.glyphs)
                            if (gbox.font && gbox.font->isHershey)
                                m_hersheyMode = true;
                }
                // Similar for TableBox, FootnoteSectionBox
            }, elem);
        }
    }
```

In the markdown mode invisible text overlay, modify the font selection for Hershey glyph boxes to use the Base 14 font. This is handled in the resource dict and font registration — covered in Task 8.

**Step 4: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 5: Commit**

```bash
git add src/pdf/pdfgenerator.h src/pdf/pdfgenerator.cpp
git commit -m "feat: wire Hershey rendering into renderLineBox for both modes"
```

---

### Task 8: Skip font embedding in Hershey mode + Base 14 fonts

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp` (embedFonts, font registration, resource dict)

**Step 1: Skip Hershey fonts in ensureFontRegistered**

In `ensureFontRegistered()`, skip Hershey fonts (they don't need embedding):

```cpp
int PdfGenerator::ensureFontRegistered(FontFace *face)
{
    if (!face) return -1;
    // Skip Hershey fonts — they render as paths, not embedded font glyphs
    if (face->isHershey) return -1;
    // ... existing code ...
}
```

**Step 2: Add Base 14 font for markdown copy invisible text**

For the invisible text layer in markdown+Hershey mode, we need to reference a PDF Base 14 font. These don't need embedding — they're built into every PDF viewer.

In the resource dictionary construction (in `renderPage` or wherever the `/Font` dictionary is built), when Hershey mode is active and markdown copy is enabled, add a Base 14 font reference:

```
/HvInv << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>
```

The exact integration point depends on how the resource dict is built. Look for where `/Font <<` is emitted and add the Base 14 entry there.

In the invisible text overlay code (lines 679-692), for Hershey glyph boxes, use the Base 14 font name instead of the embedded font name:

```cpp
            QByteArray fontName;
            if (gbox.font && gbox.font->isHershey)
                fontName = "HvInv"; // Base 14 Helvetica for invisible text
            else
                fontName = pdfFontName(gbox.font);
```

**Step 3: Handle fallback (non-Hershey) glyphs as paths too**

When in Hershey mode, even fallback system font glyphs should render as paths (no embedding). In `renderLineBox`, fallback glyph boxes already go through `renderGlyphBoxAsPath()` (which fills outlines as paths) — this is correct. But we need to ensure the fallback fonts also skip embedding.

In `embedFonts()`, add a check:
```cpp
void PdfGenerator::embedFonts(Pdf::Writer &writer)
{
    for (auto &ef : m_embeddedFonts) {
        // In Hershey mode, all visible text is paths — skip embedding
        if (m_hersheyMode)
            continue;
        ef.fontObjId = writeCidFont(writer, ef.face, ef.pdfName);
    }
}
```

Actually, more precisely: in Hershey mode, visible text is all paths. But if markdown copy is enabled, the invisible text overlay still uses Tj operators with the Base 14 font (which doesn't need embedding). So we can skip ALL font embedding in Hershey mode.

Wait — but `pdfFontName(gbox.font)` is called for fallback fonts in the markdown invisible overlay too. Those fallback fonts ARE registered. We need to either:
1. Keep them registered but skip embedding, or
2. Use the Base 14 font for ALL invisible text in Hershey mode

Option 2 is simpler and correct (the invisible text doesn't care about exact glyph shapes):

For the entire invisible text overlay in markdown+Hershey mode, always use the Base 14 font. This avoids needing to register any fonts at all.

**Step 4: In the font registration scan, skip Hershey fonts entirely**

In `generate()` (lines 92-118), the first pass registers fonts. Modify to skip Hershey:

```cpp
    for (const auto &page : layout.pages) {
        for (const auto &elem : page.elements) {
            std::visit([&](const auto &e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, Layout::BlockBox>) {
                    for (const auto &line : e.lines)
                        for (const auto &gbox : line.glyphs)
                            if (gbox.font && !gbox.font->isHershey)
                                ensureFontRegistered(gbox.font);
                }
                // ... same for TableBox, FootnoteSectionBox
```

In full Hershey mode, this results in zero registered fonts and zero embedded fonts.

**Step 5: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 6: Commit**

```bash
git add src/pdf/pdfgenerator.cpp src/pdf/pdfgenerator.h
git commit -m "feat: skip font embedding in Hershey mode, use Base 14 for invisible text"
```

---

### Task 9: Handle renderLineBox fallback glyphs as paths in Hershey mode

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp:706-735` (non-markdown rendering)

**Step 1: In non-markdown mode, render fallback glyphs as paths too**

When the theme is Hershey mode but a specific glyph box uses a fallback system font, we need to render it as filled outlines (not as embedded font text) to maintain zero font embedding.

In the non-markdown rendering branches, after the Hershey check, use `renderGlyphBoxAsPath()` for non-Hershey glyph boxes when in Hershey mode:

```cpp
            if (gbox.font && gbox.font->isHershey)
                renderHersheyGlyphBox(gbox, stream, x, baselineY);
            else if (m_hersheyMode)
                renderGlyphBoxAsPath(gbox, stream, x, baselineY); // fallback as filled paths
            else
                renderGlyphBox(gbox, stream, x, baselineY);
```

Apply this pattern to both justified and non-justified branches.

**Step 2: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 3: Commit**

```bash
git add src/pdf/pdfgenerator.cpp
git commit -m "feat: render fallback glyphs as filled paths in Hershey mode"
```

---

### Task 10: Built-in Hershey theme

**Files:**
- Create: `src/themes/hershey.json`
- Modify: `src/CMakeLists.txt:135-142` (add to themes resource)
- Modify: `src/style/thememanager.cpp` (register built-in theme)

**Step 1: Create hershey.json**

Create `src/themes/hershey.json` following the exact same structure as `default.json`:

```json
{
    "name": "Hershey",
    "version": 2,
    "description": "Retro vector stroke typography — no embedded fonts",
    "hersheyMode": true,

    "paragraphStyles": {
        "Default Paragraph Style": {
            "fontFamily": "Hershey Serif",
            "fontSize": 11.0,
            "lineHeightPercent": 100,
            "foreground": "#1a1a1a"
        },
        "Heading": {
            "parent": "Default Paragraph Style",
            "fontFamily": "Hershey Sans",
            "fontWeight": "bold",
            "alignment": "left"
        },
        "BodyText": {
            "parent": "Default Paragraph Style",
            "alignment": "justify",
            "spaceAfter": 6.0
        },
        "Heading1": {
            "parent": "Heading",
            "fontSize": 28.0,
            "spaceBefore": 24.0,
            "spaceAfter": 12.0,
            "foreground": "#1a1a2e"
        },
        "Heading2": {
            "parent": "Heading",
            "fontSize": 22.0,
            "spaceBefore": 20.0,
            "spaceAfter": 10.0,
            "foreground": "#1a1a2e"
        },
        "Heading3": {
            "parent": "Heading",
            "fontSize": 18.0,
            "spaceBefore": 16.0,
            "spaceAfter": 8.0,
            "foreground": "#1a1a2e"
        },
        "Heading4": {
            "parent": "Heading",
            "fontSize": 15.0,
            "spaceBefore": 12.0,
            "spaceAfter": 6.0,
            "foreground": "#1a1a2e"
        },
        "Heading5": {
            "parent": "Heading",
            "fontSize": 13.0,
            "spaceBefore": 10.0,
            "spaceAfter": 4.0,
            "foreground": "#1a1a2e"
        },
        "Heading6": {
            "parent": "Heading",
            "fontSize": 11.0,
            "fontItalic": true,
            "spaceBefore": 8.0,
            "spaceAfter": 4.0,
            "foreground": "#1a1a2e"
        },
        "BlockQuote": {
            "parent": "BodyText",
            "fontFamily": "Hershey Script",
            "fontItalic": true,
            "foreground": "#555555"
        },
        "ListItem": {
            "parent": "BodyText"
        },
        "OrderedListItem": {
            "parent": "ListItem"
        },
        "UnorderedListItem": {
            "parent": "ListItem"
        },
        "TaskListItem": {
            "parent": "ListItem"
        },
        "CodeBlock": {
            "parent": "Default Paragraph Style",
            "baseCharacterStyle": "Code",
            "background": "#f6f8fa",
            "lineHeightPercent": 140
        },
        "TableHeader": {
            "parent": "Default Paragraph Style",
            "fontWeight": "bold",
            "background": "#f0f0f0",
            "alignment": "center"
        },
        "TableBody": {
            "parent": "Default Paragraph Style"
        },
        "HorizontalRule": {
            "parent": "Default Paragraph Style"
        },
        "MathDisplay": {
            "parent": "Default Paragraph Style"
        }
    },

    "characterStyles": {
        "Default Character Style": {
            "fontFamily": "Hershey Serif",
            "fontSize": 11.0,
            "foreground": "#1a1a1a"
        },
        "DefaultText": {
            "parent": "Default Character Style"
        },
        "Emphasis": {
            "parent": "Default Character Style",
            "fontItalic": true
        },
        "Strong": {
            "parent": "Default Character Style",
            "fontWeight": "bold"
        },
        "StrongEmphasis": {
            "parent": "Default Character Style",
            "fontWeight": "bold",
            "fontItalic": true
        },
        "Strikethrough": {
            "parent": "Default Character Style",
            "strikeOut": true
        },
        "Subscript": {
            "parent": "Default Character Style"
        },
        "Superscript": {
            "parent": "Default Character Style"
        },
        "Code": {
            "parent": "Default Character Style",
            "fontFamily": "Hershey Roman",
            "fontSize": 10.0
        },
        "InlineCode": {
            "parent": "Code",
            "foreground": "#c7254e",
            "background": "#f0f0f0"
        },
        "Link": {
            "parent": "Default Character Style",
            "foreground": "#0366d6",
            "underline": true
        },
        "MathInline": {
            "parent": "Default Character Style"
        },
        "Emoji": {
            "parent": "Default Character Style"
        }
    },

    "tableStyles": {
        "Default": {
            "borderCollapse": true,
            "cellPadding": { "top": 3, "bottom": 3, "left": 6, "right": 6 },
            "headerBackground": "#f0f0f0",
            "alternateRowColor": "#f9f9f9",
            "outerBorder": { "width": 1.0, "color": "#333333" },
            "innerBorder": { "width": 0.5, "color": "#cccccc" },
            "headerBottomBorder": { "width": 2.0, "color": "#333333" },
            "headerParagraphStyle": "TableHeader",
            "bodyParagraphStyle": "TableBody"
        }
    },

    "footnoteStyle": {
        "format": "arabic",
        "startNumber": 1,
        "restart": "per_document",
        "prefix": "",
        "suffix": "",
        "superscriptRef": true,
        "superscriptNote": false,
        "asEndnotes": true,
        "showSeparator": true,
        "separatorWidth": 0.5,
        "separatorLength": 72.0
    },

    "masterPages": {
        "first": {
            "headerEnabled": false,
            "footerEnabled": true
        },
        "left": {
            "headerLeft": "{title}",
            "headerCenter": "",
            "headerRight": ""
        },
        "right": {
            "headerLeft": "",
            "headerCenter": "",
            "headerRight": "{title}"
        }
    }
}
```

Note: No `fontFeatures` in Hershey styles since Hershey fonts don't support OpenType features.

**Step 2: Add to themes resource in CMakeLists.txt**

In `src/CMakeLists.txt`, add `themes/hershey.json` to the themes resource block (line 141):

```cmake
qt_add_resources(PenelopeCore "themes"
    PREFIX "/themes"
    BASE themes
    FILES
        themes/default.json
        themes/academic.json
        themes/hershey.json
)
```

**Step 3: Register as built-in theme**

In `src/style/thememanager.cpp`, find the `registerBuiltinThemes()` method. Add:

```cpp
    m_themes.append({QStringLiteral("hershey"),
                     QStringLiteral("Hershey"),
                     QStringLiteral(":/themes/hershey.json")});
```

**Step 4: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 5: Commit**

```bash
git add src/themes/hershey.json src/CMakeLists.txt src/style/thememanager.cpp
git commit -m "feat: add built-in Hershey theme with stroke font styles"
```

---

### Task 11: Font selector filtering for Hershey mode

**Files:**
- Modify: `src/widgets/stylepropertieseditor.cpp:119-128` (font combo)
- Modify: `src/widgets/stylepropertieseditor.h` (add method/member)

**Step 1: Add Hershey mode awareness to StylePropertiesEditor**

The font family dropdown uses `QFontComboBox` which lists system fonts. In Hershey mode, replace it with a `QComboBox` showing only Hershey family names.

In `src/widgets/stylepropertieseditor.h`, add a method and member:
```cpp
    void setHersheyMode(bool enabled);
private:
    bool m_hersheyMode = false;
    QComboBox *m_hersheyFontCombo = nullptr; // shown in Hershey mode
```

In `src/widgets/stylepropertieseditor.cpp`:

1. Create the Hershey font combo alongside the system font combo, initially hidden:
```cpp
    m_hersheyFontCombo = new QComboBox;
    m_hersheyFontCombo->setVisible(false);
    fontRow->addWidget(m_hersheyFontCombo, 1);
```

2. Implement `setHersheyMode()`:
```cpp
void StylePropertiesEditor::setHersheyMode(bool enabled)
{
    m_hersheyMode = enabled;
    m_fontCombo->setVisible(!enabled);
    m_hersheyFontCombo->setVisible(enabled);
    if (enabled) {
        m_hersheyFontCombo->clear();
        HersheyFontRegistry::instance().ensureLoaded();
        for (const QString &name : HersheyFontRegistry::instance().familyNames())
            m_hersheyFontCombo->addItem(name);
    }
}
```

3. Connect the Hershey font combo to the same style-update logic. When the user selects a Hershey family, emit the same signal/update as the QFontComboBox.

4. In `loadParagraphStyle` and `loadCharacterStyle`, set the correct combo based on mode:
```cpp
    if (m_hersheyMode) {
        int idx = m_hersheyFontCombo->findText(resolved.fontFamily());
        if (idx >= 0) m_hersheyFontCombo->setCurrentIndex(idx);
    } else {
        m_fontCombo->setCurrentFont(QFont(resolved.fontFamily()));
    }
```

**Step 2: Propagate hersheyMode from StyleDockWidget**

In `src/widgets/styledockwidget.cpp`, when the theme changes, call `m_editor->setHersheyMode(styleManager->hersheyMode())` after loading the theme.

**Step 3: Verify build**

Run: `cmake --build build --parallel`
Expected: Clean build.

**Step 4: Commit**

```bash
git add src/widgets/stylepropertieseditor.cpp src/widgets/stylepropertieseditor.h src/widgets/styledockwidget.cpp
git commit -m "feat: filter font selector to Hershey families when Hershey mode active"
```

---

### Task 12: Integration testing and tuning

**Files:** No new files — verification and tuning pass.

**Step 1: Full build and run**

```bash
cmake --build build --parallel
./build/src/Penelope
```

**Step 2: Test with Hershey theme**

1. Open a markdown file with headings, body text, bold, italic, code blocks, lists, links, tables
2. Switch to the "Hershey" theme in the style dock
3. Verify: text renders as stroked paths on screen
4. Verify: headings use Hershey Sans, body uses Hershey Serif, code uses Hershey Roman
5. Verify: bold text appears with thicker strokes
6. Verify: italic text appears with visible skew
7. Verify: underline and strikethrough still work

**Step 3: Test PDF export**

1. Export to PDF
2. Open in a PDF viewer
3. Verify: text is visible and properly positioned
4. Verify: no font embedding (check PDF file size — should be notably smaller)
5. Verify: text selection works if markdown copy mode is enabled

**Step 4: Test font selector**

1. With Hershey theme active, open Style dock → edit a paragraph style
2. Verify: font dropdown shows only Hershey families
3. Switch to Default theme
4. Verify: font dropdown shows system fonts again

**Step 5: Tune stroke rendering**

Based on visual inspection, adjust:
- `baseStrokeWidth` constant in `renderHersheyGlyphBox` (currently 0.02 × fontSize)
- Bold multiplier (currently 1.8)
- Italic skew angle (currently tan(12°) ≈ 0.2126)
- Glyph vertical positioning (ascent/descent might need adjustment based on visual baseline)

**Step 6: Fix any issues found**

Address rendering artifacts, metrics misalignment, or other visual issues.

**Step 7: Commit any tuning changes**

```bash
git add -u
git commit -m "fix: tune Hershey stroke rendering parameters"
```

---

### Task 13: Handle edge cases

**Files:** Various, depends on issues found.

**Step 1: Header/footer rendering in Hershey mode**

The header/footer renderer (`renderHeaderFooter` in pdfgenerator.cpp) loads its own fonts. Ensure it also handles Hershey fonts or falls back correctly.

Check `src/pdf/pdfgenerator.cpp` for the `renderHeaderFooter` method and verify it uses the same rendering path.

**Step 2: Table cell rendering**

Tables go through `renderTableBox` → `renderLineBox` for each cell. Since we already modified `renderLineBox`, tables should work. Verify with a markdown table.

**Step 3: Code block rendering**

Code blocks use the `Code` character style which maps to "Hershey Roman" in the Hershey theme. Verify syntax-highlighted code renders correctly (colors should still work since we set stroke color from foreground).

**Step 4: Footnote rendering**

Footnotes go through `renderFootnoteSectionBox` → `renderLineBox`. Should work via the same path. Verify with a document containing footnotes.

**Step 5: Commit**

```bash
git add -u
git commit -m "fix: handle Hershey mode edge cases in tables, headers, footnotes"
```
