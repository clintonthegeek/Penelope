# Hershey Stroke Fonts Design

## Overview

Add Hershey vector stroke fonts as a first-class alternative rendering mode. When a theme enables Hershey mode, all text is rendered as stroked polyline paths in the PDF — no font embedding required. This produces tiny, retro-styled PDFs where every character is a vector drawing.

Source font data: [kamalmostafa/hershey-fonts](https://github.com/kamalmostafa/hershey-fonts) (GPLv2+, glyph data permissively licensed).

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Toggle scope | Per-theme (`"hersheyMode": true` in theme JSON) | Allows Hershey and regular themes to coexist; user switches by changing theme |
| Render scope | Both screen and PDF | Screen already goes through PDF→Poppler pipeline; natural consistency |
| Font families | Full curated set (Sans, Roman, Serif, Script, Gothic×3, Greek, Cyrillic) | All 32 .jhf fonts organized into logical families |
| Style synthesis | Prefer native variant, fallback to stroke width/skew | Authentic shapes when available (e.g. `timesib` for bold-italic serif), synthesis otherwise |
| Missing glyphs | System font fallback rendered as vector outlines | Readability over purity; uses existing `renderGlyphBoxAsPath()` |
| Font embedding | Zero embedded fonts | Hershey = stroked paths, fallback = filled paths, invisible text layer = PDF Base 14 fonts |
| Data bundling | Qt resources (.qrc) | Follows existing pattern for themes/dicts/fonts; ~200KB for all 32 fonts |
| Built-in theme | Yes, ship `hershey.json` | Immediate discoverability |

## Architecture

### Integration approach: PDF Generator Rendering Mode

The existing pipeline (parse → style → layout → PDF) stays intact. Changes concentrate in:
1. Font metrics routing for layout
2. PDF stroke rendering for Hershey glyphs
3. Theme/style filtering for Hershey families

### Data Layer

**New files:** `src/font/hersheyfont.h`, `src/font/hersheyfont.cpp`

`HersheyFont` class:
- Loads a `.jhf` file from Qt resources (`:/hershey/*.jhf`)
- Parses JHF format: 5-char glyph ID, 3-char vertex count, left/right boundaries, ASCII-encoded coordinate pairs (`char - 'R'`), pen-up marker (` R`)
- Stores glyphs as:
  ```cpp
  struct HersheyGlyph {
      int leftBound, rightBound;
      QVector<QVector<QPointF>> strokes; // list of polylines
  };
  ```
- Provides metrics: advance width (`rightBound - leftBound`), ascent/descent (from bounding box scan), line height
- `hasGlyph(char32_t)` for fallback decisions

`HersheyFontRegistry` (static/singleton):
- Lazy-loads all 32 .jhf fonts on first Hershey theme activation
- Maps `(familyName, weight, italic)` → concrete `.jhf` font + synthesis flags

**Family mapping:**

| Family | Normal | Bold | Italic | Bold Italic |
|---|---|---|---|---|
| Hershey Sans | futural | futuram | futural+skew | futuram+skew |
| Hershey Roman | rowmans | rowmant | rowmans+skew | rowmant+skew |
| Hershey Serif | timesr | timesrb | timesi | timesib |
| Hershey Script | scripts | scriptc | scripts+skew | scriptc+skew |
| Hershey Gothic English | gothiceng | gothiceng+stroke | gothiceng+skew | gothiceng+both |
| Hershey Gothic German | gothicger | gothgbt | gothicger+skew | gothgbt+skew |
| Hershey Gothic Italian | gothicita | gothitt | gothicita+skew | gothitt+skew |
| Hershey Greek | greek | greekc | greeks | greekc+skew |
| Hershey Cyrillic | cyrillic | cyrilc_1 | cyrillic+skew | cyrilc_1+skew |

Synthesis: bold = 1.8-2.0× stroke width, italic = 12° skew transform.

### Theme & Style Integration

**Theme JSON:** Add `"hersheyMode": true` top-level flag. Styles reference Hershey family names in `fontFamily`.

**ThemeManager:** Parse `hersheyMode` flag, propagate to StyleManager.

**StyleManager:** Store `bool hersheyMode()` accessor.

**Style Dock Widget:** When `hersheyMode` active, font family dropdown shows only Hershey families.

**ContentBuilder:** No changes — `fontFamily` is already a string, works with `"Hershey Serif"` etc.

### Layout Engine Integration

**FontManager changes:**
- `loadFont(family, weight, italic)`: if `family.startsWith("Hershey ")`, route to HersheyFontRegistry
- Return a `FontFace*` with `isHershey = true` flag
- Metric methods (`ascent()`, `descent()`, `glyphWidth()`) delegate to HersheyFont when `isHershey`

**TextShaper changes:**
- Detect Hershey FontFace and bypass HarfBuzz
- Produce simple 1:1 character→glyph mapping
- Glyph ID = codepoint value
- Advance widths from HersheyFont metrics
- No kerning, ligatures, or BiDi for Hershey runs
- Missing glyph coverage → fallback to system font for that run (existing logic)

**Layout engine:** No changes. Already works with GlyphInfo (glyphId, xAdvance, xOffset, yOffset).

### PDF Rendering

**New method:** `PdfGenerator::renderHersheyGlyphBox()`

Called from `renderLineBox()` when glyph box font is Hershey. For each glyph:
1. Look up `HersheyGlyph` by codepoint
2. Scale: `scale = fontSize / hersheyUnitsPerEm`
3. Translate to glyph position
4. Emit each stroke as: `x0 y0 m x1 y1 l x2 y2 l ... S`

**Stroke styling:**
- Base width: `~0.02 × fontSize` (tunable)
- Bold synthesis: width × 1.8-2.0
- Italic synthesis: `1 0 tan(12°) 1 tx ty cm` skew transform
- Line cap: round (`1 J`), line join: round (`1 j`)
- Color: `gbox.style.foreground`

**Fallback glyphs:** Non-Hershey glyph boxes use existing `renderGlyphBoxAsPath()` (FreeType outline decomposition → filled paths). No font embedding.

**Font embedding:** `embedFonts()` skips all fonts in Hershey mode. Zero font objects in PDF.

**Markdown copy mode:** Invisible text layer uses a PDF Base 14 font (e.g. Helvetica) — guaranteed available without embedding. Rendering mode 3 (invisible) as today.

**Underline/strikethrough:** Unchanged — already horizontal line segments.

### Built-in Theme

`src/themes/hershey.json`:
- Body: Hershey Serif, 11pt
- Headings: Hershey Sans, scaled sizes
- Code blocks: Hershey Roman (simplex monospace-ish)
- Block quotes: Hershey Script, italic
- All standard style hierarchy (inherits Default Paragraph Style pattern)

### Resource Bundling

32 .jhf files in `src/hershey/` directory, added via `qt_add_resources(PenelopeCore "hershey" PREFIX "/" FILES ...)` in `src/CMakeLists.txt`.

## Change Map

| Component | Change | Scope |
|---|---|---|
| `src/font/hersheyfont.h/cpp` | NEW: JHF parser, glyph data, metrics, font registry | ~400-500 lines |
| `src/font/fontmanager.cpp/h` | Route Hershey families; `isHershey` on FontFace | Small |
| `src/font/textshaper.cpp` | Bypass HarfBuzz for Hershey runs | Small |
| `src/pdf/pdfgenerator.cpp/h` | `renderHersheyGlyphBox()`; skip embedding; Base14 invisible text | ~150-200 lines |
| `src/style/thememanager.cpp/h` | Parse `hersheyMode` from JSON | Small |
| `src/style/stylemanager.h` | Store `hersheyMode` flag | Trivial |
| `src/widgets/styledockwidget.cpp` | Filter font selector for Hershey families | Small |
| `src/themes/hershey.json` | NEW: Built-in Hershey theme | ~100 lines |
| `src/CMakeLists.txt` | Add hershey resources, new source files | Small |
| `src/hershey/*.jhf` | NEW: 32 font data files | ~200KB |

**Not changed:** Layout engine, content model, content builder, preferences dialog, page layout.
