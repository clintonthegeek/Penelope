# TTF XObject Glyph Rendering Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend XObject Form glyph rendering to TTF/OTF fonts, so all visible glyphs are rendered as reusable vector XObjects with no text operators in the visible layer.

**Architecture:** Extend `ensureGlyphForm` with a FreeType branch that decomposes TrueType outlines into Form XObject streams. Add `xobjectGlyphs` flag to `PdfExportOptions`. When enabled, all fonts (Hershey and TTF) render via `cm` + `Do`, CIDFont embedding is skipped, and invisible text uses Base14 Helvetica.

**Tech Stack:** C++/Qt6, FreeType (`FT_Outline_Decompose`), raw PDF stream operators

---

### Task 1: Add xobjectGlyphs flag to PdfExportOptions

**Files:**
- Modify: `src/model/pdfexportoptions.h`

**Step 1: Add the flag**

After the `unwrapParagraphs` field (line 21), add:

```cpp
    bool xobjectGlyphs = false;    // render all glyphs as Form XObjects (no text operators)
```

**Step 2: Build and verify**

Run: `cmake --build build --target Penelope 2>&1 | tail -5`
Expected: Clean build

**Step 3: Commit**

```
feat: add xobjectGlyphs flag to PdfExportOptions
```

---

### Task 2: Add xobjectGlyphs checkbox to PDF export dialog

**Files:**
- Modify: `src/widgets/pdfexportdialog.h`
- Modify: `src/widgets/pdfexportdialog.cpp`

**Step 1: Add member in header**

In `pdfexportdialog.h`, after `m_unwrapParagraphsCheck` (line 54), add:

```cpp
    QCheckBox *m_xobjectGlyphsCheck = nullptr;
```

**Step 2: Create the checkbox in setupGeneralPage()**

In `pdfexportdialog.cpp`, after the `m_unwrapParagraphsCheck` row (line 88), add:

```cpp
    m_xobjectGlyphsCheck = new QCheckBox(i18n("Render glyphs as vector art"), copyGroup);
    m_xobjectGlyphsCheck->setToolTip(
        i18n("Draws all font glyphs as reusable vector shapes instead of text operators. "
             "Produces smaller files and prevents visible text from interfering with "
             "markdown copy. Recommended when 'Embed markdown syntax' is enabled."));
    copyForm->addRow(m_xobjectGlyphsCheck);
```

**Step 3: Wire it in the options() getter**

In `pdfexportdialog.cpp`, after `opts.unwrapParagraphs = ...` (line 389), add:

```cpp
    opts.xobjectGlyphs = m_xobjectGlyphsCheck->isChecked();
```

**Step 4: Wire it in setOptions()**

In `pdfexportdialog.cpp`, after `m_unwrapParagraphsCheck->setChecked(...)` (line 431), add:

```cpp
    m_xobjectGlyphsCheck->setChecked(opts.xobjectGlyphs);
```

**Step 5: Build and verify**

Run: `cmake --build build --target Penelope 2>&1 | tail -5`

**Step 6: Commit**

```
feat: add xobjectGlyphs checkbox to PDF export dialog
```

---

### Task 3: Extend GlyphFormKey to support TTF fonts

**Files:**
- Modify: `src/pdf/pdfgenerator.h`

**Step 1: Add ttfFace to GlyphFormKey**

Replace the current `GlyphFormKey` struct (lines 110-117) with:

```cpp
    struct GlyphFormKey {
        const HersheyFont *hersheyFont = nullptr;
        FontFace *ttfFace = nullptr;
        uint glyphId = 0;
        bool bold = false;

        bool operator==(const GlyphFormKey &o) const {
            return hersheyFont == o.hersheyFont && ttfFace == o.ttfFace
                   && glyphId == o.glyphId && bold == o.bold;
        }
    };
```

**Step 2: Update qHash to include ttfFace**

Replace the `qHash` friend function (line 119-121) with:

```cpp
    friend size_t qHash(const PdfGenerator::GlyphFormKey &k, size_t seed = 0) {
        return qHashMulti(seed, quintptr(k.hersheyFont), quintptr(k.ttfFace),
                          k.glyphId, k.bold);
    }
```

**Step 3: Update ensureGlyphForm declaration**

Replace the current declaration (line 131) with:

```cpp
    GlyphFormEntry ensureGlyphForm(const HersheyFont *hersheyFont,
                                   FontFace *ttfFace,
                                   uint glyphId, bool bold);
```

**Step 4: Add renderTtfGlyphBoxAsXObject declaration**

After the `renderHersheyGlyphBox` declaration (line 58-59), add:

```cpp
    void renderTtfGlyphBoxAsXObject(const Layout::GlyphBox &gbox, QByteArray &stream,
                                     qreal x, qreal y);
```

**Step 5: Build (expect errors — callers not updated yet)**

Run: `cmake --build build --target Penelope 2>&1 | tail -20`
Expected: Compilation errors from `ensureGlyphForm` callers — this is correct, we fix them in the next tasks.

**Step 6: Commit**

```
feat: extend GlyphFormKey and ensureGlyphForm for TTF support
```

---

### Task 4: Update ensureGlyphForm with TTF branch

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Update existing ensureGlyphForm signature and Hershey callers**

The function signature changes from:
```cpp
PdfGenerator::GlyphFormEntry PdfGenerator::ensureGlyphForm(
    const HersheyFont *font, uint glyphId, bool bold)
```
to:
```cpp
PdfGenerator::GlyphFormEntry PdfGenerator::ensureGlyphForm(
    const HersheyFont *hersheyFont, FontFace *ttfFace,
    uint glyphId, bool bold)
```

**Step 2: Update the function body**

The function needs:
1. Updated null check: `if ((!hersheyFont && !ttfFace) || !m_writer || !m_resources) return {};`
2. Updated key construction: `GlyphFormKey key{hersheyFont, ttfFace, glyphId, bold};`
3. After the existing Hershey branch, add a TTF branch:

```cpp
    } else if (ttfFace && ttfFace->ftFace) {
        // TTF: decompose FreeType outline into filled paths (in font units)
        FT_Face face = ttfFace->ftFace;
        if (FT_Load_Glyph(face, glyphId, FT_LOAD_NO_SCALE) != 0
            || face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
            return {};
        }

        // Outline callbacks write to formStream in font units (no scaling)
        FT_Outline_Funcs funcs = {};
        funcs.move_to = outlineMoveTo;
        funcs.line_to = outlineLineTo;
        funcs.conic_to = outlineConicTo;
        funcs.cubic_to = outlineCubicTo;

        OutlineCtx ctx;
        ctx.stream = &formStream;
        ctx.scale = 1.0;  // font units — scaling at call site via cm
        ctx.tx = 0;
        ctx.ty = 0;
        ctx.last = {0, 0};
        FT_Outline_Decompose(&face->glyph->outline, &funcs, &ctx);
        formStream += "f\n";  // fill (not stroke)

        advW = face->glyph->metrics.horiAdvance;
        bboxBottom = face->bbox.yMin;
        bboxTop = face->bbox.yMax;
        bboxLeft = 0;
        bboxRight = face->glyph->metrics.horiAdvance;
    }
```

The full function structure becomes:

```cpp
GlyphFormEntry ensureGlyphForm(hersheyFont, ttfFace, glyphId, bold) {
    // null checks
    // cache lookup

    QByteArray formStream;
    qreal advW = 0, bboxBottom = 0, bboxTop = 0, bboxLeft = 0, bboxRight = 0;

    if (hersheyFont) {
        // existing Hershey branch (stroked paths)
        // sets advW, bboxBottom, bboxTop
    } else if (ttfFace && ttfFace->ftFace) {
        // new TTF branch (filled outlines)
        // sets advW, bboxBottom, bboxTop
    } else {
        return {};
    }

    // Write XObject (shared for both branches)
    // Register in cache and resources (shared)
}
```

IMPORTANT: The `OutlineCtx` struct and outline callbacks (`outlineMoveTo`, etc.) are in an anonymous namespace near the top of the file (~lines 530-583). They must be accessible from `ensureGlyphForm`. Since both are in the same `.cpp` file, this works as-is.

**Step 3: Update existing Hershey callers**

In `renderHersheyGlyphBox`, change:
```cpp
auto entry = ensureGlyphForm(hFont, g.glyphId, gbox.font->hersheyBold);
```
to:
```cpp
auto entry = ensureGlyphForm(hFont, nullptr, g.glyphId, gbox.font->hersheyBold);
```

Do the same for the trailing Hershey hyphen call site (~line 833).

**Step 4: Build and verify**

Run: `cmake --build build --target Penelope 2>&1 | tail -20`
Expected: Compiles (TTF branch exists but isn't called yet)

**Step 5: Commit**

```
feat: add FreeType outline branch to ensureGlyphForm
```

---

### Task 5: Implement renderTtfGlyphBoxAsXObject

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Add the function**

Place it after `renderGlyphBoxAsPath` (which ends around line 1112). It follows the same structure as `renderHersheyGlyphBox` but for TTF:

```cpp
void PdfGenerator::renderTtfGlyphBoxAsXObject(const Layout::GlyphBox &gbox,
                                                QByteArray &stream,
                                                qreal x, qreal y)
{
    if (gbox.glyphs.isEmpty() || !gbox.font || !gbox.font->ftFace)
        return;

    FT_Face face = gbox.font->ftFace;
    qreal scale = gbox.fontSize / face->units_per_EM;

    // Background (inline code highlight)
    if (gbox.style.background.isValid()) {
        stream += "q\n";
        stream += colorOperator(gbox.style.background, true);
        stream += pdfCoord(x - 1) + " " + pdfCoord(y - gbox.descent - 1) + " "
                + pdfCoord(gbox.width + 2) + " " + pdfCoord(gbox.ascent + gbox.descent + 2)
                + " re f\n";
        stream += "Q\n";
    }

    qreal curX = x;
    for (const auto &g : gbox.glyphs) {
        auto entry = ensureGlyphForm(nullptr, gbox.font, g.glyphId, false);
        if (entry.objId == 0) {
            curX += g.xAdvance;
            continue;
        }

        qreal gx = curX + g.xOffset;
        qreal gy = y - g.yOffset;

        if (gbox.style.superscript)
            gy += gbox.fontSize * 0.35;
        else if (gbox.style.subscript)
            gy -= gbox.fontSize * 0.15;

        stream += "q\n";
        stream += colorOperator(gbox.style.foreground, true); // fill color (rg)
        stream += pdfCoord(scale) + " 0 0 " + pdfCoord(scale)
                + " " + pdfCoord(gx) + " " + pdfCoord(gy) + " cm\n";
        stream += "/" + entry.pdfName + " Do\n";
        stream += "Q\n";

        curX += g.xAdvance;
    }

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

    // Collect link annotation rect
    if (!gbox.style.linkHref.isEmpty()) {
        collectLinkRect(x, y, curX - x, gbox.ascent, gbox.descent,
                        gbox.style.linkHref);
    }
}
```

Note: TTF bold is a different `FontFace` (not a synthesis flag), so `bold` is always `false`. TTF italic is also a different face — no skew needed. Fill color uses `colorOperator(color, true)` (the `rg` operator), unlike Hershey which uses stroke color (`RG`).

**Step 2: Build and verify**

Run: `cmake --build build --target Penelope 2>&1 | tail -5`

**Step 3: Commit**

```
feat: implement renderTtfGlyphBoxAsXObject
```

---

### Task 6: Update rendering dispatch in renderLineBox

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Add xobjectGlyphs shorthand**

Near the top of `renderLineBox`, after `bool markdownMode = m_exportOptions.markdownCopy;` (around line 645), add:

```cpp
    bool xobjectGlyphs = m_exportOptions.xobjectGlyphs;
```

**Step 2: Update the three rendering dispatch sites**

There are three places in `renderLineBox` where glyph rendering is dispatched. Each has the pattern:

```cpp
if (gbox.font->isHershey) renderHersheyGlyphBox(...)
else if (m_hersheyMode) renderGlyphBoxAsPath(...)
else renderGlyphBox(...)
```

Add `xobjectGlyphs` before the existing fallbacks. In all three sites:

**Site 1: Markdown mode visible path rendering (around line 771-775)**
```cpp
        for (int i = 0; i < line.glyphs.size(); ++i) {
            if (line.glyphs[i].font && line.glyphs[i].font->isHershey)
                renderHersheyGlyphBox(line.glyphs[i], stream, glyphXPositions[i], baselineY);
            else if (xobjectGlyphs)
                renderTtfGlyphBoxAsXObject(line.glyphs[i], stream, glyphXPositions[i], baselineY);
            else
                renderGlyphBoxAsPath(line.glyphs[i], stream, glyphXPositions[i], baselineY);
        }
```

**Site 2: Justify mode (around line 785-790)**
```cpp
            if (line.glyphs[i].font && line.glyphs[i].font->isHershey)
                renderHersheyGlyphBox(line.glyphs[i], stream, x, baselineY);
            else if (xobjectGlyphs)
                renderTtfGlyphBoxAsXObject(line.glyphs[i], stream, x, baselineY);
            else if (m_hersheyMode)
                renderGlyphBoxAsPath(line.glyphs[i], stream, x, baselineY);
            else
                renderGlyphBox(line.glyphs[i], stream, x, baselineY);
```

**Site 3: Normal alignment mode (around line 816-821)**
```cpp
            if (gbox.font && gbox.font->isHershey)
                renderHersheyGlyphBox(gbox, stream, x, baselineY);
            else if (xobjectGlyphs)
                renderTtfGlyphBoxAsXObject(gbox, stream, x, baselineY);
            else if (m_hersheyMode)
                renderGlyphBoxAsPath(gbox, stream, x, baselineY);
            else
                renderGlyphBox(gbox, stream, x, baselineY);
```

**Step 3: Update trailing FreeType hyphen**

In the trailing hyphen section (around line 849-881), the FreeType branch currently has two paths (markdownMode vs normal). When `xobjectGlyphs` is true, use XObject Do instead:

Replace the `else if (lastGbox.font->ftFace)` block with:

```cpp
            } else if (lastGbox.font->ftFace) {
                FT_UInt hyphenGid = FT_Get_Char_Index(lastGbox.font->ftFace, '-');
                if (xobjectGlyphs) {
                    auto entry = ensureGlyphForm(nullptr, lastGbox.font, hyphenGid, false);
                    if (entry.objId != 0) {
                        qreal scale = lastGbox.fontSize / lastGbox.font->ftFace->units_per_EM;
                        stream += "q\n";
                        stream += colorOperator(lastGbox.style.foreground, true);
                        stream += pdfCoord(scale) + " 0 0 " + pdfCoord(scale)
                                + " " + pdfCoord(x) + " " + pdfCoord(baselineY) + " cm\n";
                        stream += "/" + entry.pdfName + " Do\n";
                        stream += "Q\n";
                    }
                } else if (markdownMode) {
```

(Keep the existing markdownMode and normal branches as-is, just nested under `else if`.)

**Step 4: Build and verify**

Run: `cmake --build build --target Penelope 2>&1 | tail -5`

**Step 5: Commit**

```
feat: route TTF glyphs through XObject rendering when xobjectGlyphs enabled
```

---

### Task 7: Skip CIDFont embedding in XObject mode

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Skip font embedding when xobjectGlyphs is true**

In `generate()`, the font embedding call is at line 170:
```cpp
    embedFonts(writer);
```

Change to:
```cpp
    if (!m_exportOptions.xobjectGlyphs)
        embedFonts(writer);
```

**Step 2: Provide Base14 Helvetica for invisible text in XObject mode**

The existing condition for HvInv Helvetica (line 184-189) is:
```cpp
    if (m_hersheyMode && m_exportOptions.markdownCopy) {
```

Extend it to also cover XObject mode:
```cpp
    if ((m_hersheyMode || m_exportOptions.xobjectGlyphs) && m_exportOptions.markdownCopy) {
```

**Step 3: Skip font resource dict entries in XObject mode**

The resource dict population at lines 177-179:
```cpp
    for (auto &ef : m_embeddedFonts) {
        if (ef.fontObjId)
            resources.fonts[ef.pdfName] = ef.fontObjId;
    }
```

Wrap with:
```cpp
    if (!m_exportOptions.xobjectGlyphs) {
        for (auto &ef : m_embeddedFonts) {
            if (ef.fontObjId)
                resources.fonts[ef.pdfName] = ef.fontObjId;
        }
    }
```

**Step 4: Update markdown invisible text font selection**

In `renderLineBox`, the markdown invisible text overlay selects the font for `Tf`:
- Hershey uses `HvInv` (Helvetica)
- Non-Hershey uses the embedded CIDFont

When `xobjectGlyphs` is true, ALL fonts should use `HvInv`. Search for where the invisible text font is selected (around line 743-745, look for `"HvInv"` vs `pdfFontName`):

The condition that selects HvInv vs CIDFont needs to also check `xobjectGlyphs`. Change the font selection so that:
- If `gbox.font->isHershey` OR `xobjectGlyphs`: use `"HvInv"`
- Else: use `pdfFontName(gbox.font)`

**Step 5: Build and verify**

Run: `cmake --build build --target Penelope 2>&1 | tail -5`

**Step 6: Commit**

```
feat: skip CIDFont embedding and use Base14 Helvetica in XObject mode
```

---

### Task 8: Build verification and visual testing

**Step 1: Full build**

Run: `cmake --build build --target Penelope 2>&1 | tail -10`
Expected: Clean, no warnings

**Step 2: Test with xobjectGlyphs OFF (regression)**

Export a PDF with default settings. Verify:
- [ ] Regular TTF fonts render normally via CIDFont text operators
- [ ] Hershey fonts still use XObject Do (Phase 1)
- [ ] Text is selectable in the PDF
- [ ] File size is normal

**Step 3: Test with xobjectGlyphs ON**

Export a PDF with "Render glyphs as vector art" checked. Verify:
- [ ] All glyphs (TTF and Hershey) render as vector shapes
- [ ] Text is NOT selectable from visible glyphs (just shapes)
- [ ] Colors, bold, italic render correctly
- [ ] Backgrounds (inline code) render
- [ ] Underline and strikethrough work
- [ ] Justified text spacing unchanged
- [ ] Soft hyphens render
- [ ] File size is smaller (no CIDFont data)

**Step 4: Test xobjectGlyphs + markdownCopy**

Export with both flags. Verify:
- [ ] Visible layer: vector XObjects (not selectable)
- [ ] Copy-paste from PDF returns markdown text
- [ ] ActualText layer works correctly

**Step 5: Commit any fixes**

```
fix: TTF XObject rendering visual fixes
```
