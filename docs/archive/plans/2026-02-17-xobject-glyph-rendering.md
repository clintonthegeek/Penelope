# XObject Form Glyph Rendering Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace inline Hershey stroke rendering with reusable PDF Form XObjects, eliminating repeated path data and cleanly separating visible art from text extraction.

**Architecture:** Lazy glyph-form cache in `PdfGenerator` creates Form XObjects on first use. Each Hershey glyph (per font + codepoint + bold) is defined once as a Form XObject with stroke operators in glyph-local coordinates. Call sites emit `q cm /HGn Do Q` instead of inline path operators. Color and italic skew are applied externally at the call site.

**Tech Stack:** C++/Qt6, raw PDF stream operators, zlib compression via `Pdf::Writer`

---

### Task 1: Add GlyphForm data structures to PdfGenerator header

**Files:**
- Modify: `src/pdf/pdfgenerator.h`

**Step 1: Add the GlyphFormKey and GlyphFormEntry structs**

Add after the `EmbeddedImage` struct (after line 108), before the `m_embeddedImages` member:

```cpp
    // Glyph Form XObjects (reusable vector glyph drawings)
    struct GlyphFormKey {
        const HersheyFont *font = nullptr;
        uint glyphId = 0;
        bool bold = false;

        bool operator==(const GlyphFormKey &o) const {
            return font == o.font && glyphId == o.glyphId && bold == o.bold;
        }
    };
    friend size_t qHash(const PdfGenerator::GlyphFormKey &k, size_t seed = 0) {
        return qHashMulti(seed, quintptr(k.font), k.glyphId, k.bold);
    }

    struct GlyphFormEntry {
        Pdf::ObjId objId = 0;
        QByteArray pdfName;    // "HG0", "HG1", ...
        qreal advanceWidth = 0; // in glyph units
    };

    QHash<GlyphFormKey, GlyphFormEntry> m_glyphForms;
    int m_nextGlyphFormIdx = 0;
```

**Step 2: Add writer/resources pointers and ensureGlyphForm declaration**

Add to the private section, near the other member pointers (after `m_hersheyMode`, line 141):

```cpp
    Pdf::Writer *m_writer = nullptr;           // set during generate(), null otherwise
    Pdf::ResourceDict *m_resources = nullptr;  // set during generate(), null otherwise
```

Add the method declaration near `renderHersheyGlyphBox` (after line 59):

```cpp
    GlyphFormEntry ensureGlyphForm(const HersheyFont *font, uint glyphId, bool bold);
```

**Step 3: Build and verify it compiles**

Run: `cmake --build build --target Penelope 2>&1 | tail -20`
Expected: Compiles cleanly (no uses of the new types yet)

**Step 4: Commit**

```
feat: add GlyphForm data structures to PdfGenerator header
```

---

### Task 2: Implement ensureGlyphForm — lazy Form XObject creation

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Implement ensureGlyphForm**

Add after `renderHersheyGlyphBox` (after line ~1235):

```cpp
PdfGenerator::GlyphFormEntry PdfGenerator::ensureGlyphForm(
    const HersheyFont *font, uint glyphId, bool bold)
{
    GlyphFormKey key{font, glyphId, bold};
    auto it = m_glyphForms.find(key);
    if (it != m_glyphForms.end())
        return it.value();

    const HersheyGlyph *hGlyph = font->glyph(static_cast<char32_t>(glyphId));
    if (!hGlyph) {
        // Return a dummy entry — caller will skip rendering
        GlyphFormEntry dummy;
        return dummy;
    }

    // Build the Form stream in glyph-local coordinates.
    // Origin: left baseline (x=0 at leftBound, y=0 at baseline).
    // Coordinates are in Hershey font units (scaled at call site).
    QByteArray formStream;
    formStream += "1 J 1 j\n"; // round cap & join

    qreal strokeWidth = 0.5; // base stroke in glyph units
    if (bold)
        strokeWidth *= 1.8;
    formStream += pdfCoord(strokeWidth) + " w\n";

    for (const auto &stroke : hGlyph->strokes) {
        if (stroke.size() < 2)
            continue;
        qreal px = stroke[0].x() - hGlyph->leftBound;
        qreal py = stroke[0].y();
        formStream += pdfCoord(px) + " " + pdfCoord(py) + " m\n";
        for (int si = 1; si < stroke.size(); ++si) {
            px = stroke[si].x() - hGlyph->leftBound;
            py = stroke[si].y();
            formStream += pdfCoord(px) + " " + pdfCoord(py) + " l\n";
        }
        formStream += "S\n";
    }

    // Compute BBox in glyph units
    qreal advW = hGlyph->rightBound - hGlyph->leftBound;
    qreal bboxBottom = -font->descent(); // negative (below baseline)
    qreal bboxTop = font->ascent();      // positive (above baseline)

    // Write the Form XObject
    Pdf::ObjId objId = m_writer->startObj();
    m_writer->write("<<\n/Type /XObject\n/Subtype /Form\n");
    m_writer->write("/BBox [0 " + pdfCoord(bboxBottom) + " "
                    + pdfCoord(advW) + " " + pdfCoord(bboxTop) + "]\n");
    m_writer->endObjectWithStream(objId, formStream);

    // Register in resource dict
    GlyphFormEntry entry;
    entry.objId = objId;
    entry.pdfName = "HG" + QByteArray::number(m_nextGlyphFormIdx++);
    entry.advanceWidth = advW;
    m_glyphForms.insert(key, entry);

    (*m_resources).xObjects[entry.pdfName] = objId;

    return entry;
}
```

**Step 2: Build and verify it compiles**

Run: `cmake --build build --target Penelope 2>&1 | tail -20`
Expected: Compiles cleanly (ensureGlyphForm exists but isn't called yet)

**Step 3: Commit**

```
feat: implement ensureGlyphForm for lazy Form XObject creation
```

---

### Task 3: Wire writer/resources pointers in generate()

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Set m_writer and m_resources at start of generate()**

In `generate()`, after `writer.writeHeader();` (line 88), add:

```cpp
    m_writer = &writer;
```

After building the resource dict (line 179, after `resources.xObjects[ei.pdfName] = ei.objId;`), add:

```cpp
    m_resources = &resources;
```

**Step 2: Clear pointers at end of generate()**

Before the `return output;` at the end of `generate()`, add:

```cpp
    m_writer = nullptr;
    m_resources = nullptr;
```

**Step 3: Reset glyph form cache at start of generate()**

In `generate()`, with the other `.clear()` calls (around line 75-79), add:

```cpp
    m_glyphForms.clear();
    m_nextGlyphFormIdx = 0;
```

**Step 4: Build and verify**

Run: `cmake --build build --target Penelope 2>&1 | tail -20`
Expected: Compiles cleanly

**Step 5: Commit**

```
feat: wire writer/resources pointers for lazy glyph form creation
```

---

### Task 4: Rewrite renderHersheyGlyphBox to use XObject Do

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp` (function `renderHersheyGlyphBox`, lines 1117-1235)

**Step 1: Replace the stroke rendering body with XObject Do calls**

Replace the glyph rendering loop (the section from `qreal curX = x;` through the closing `stream += "Q\n";`, lines 1149-1206) with:

```cpp
    qreal curX = x;
    for (const auto &g : gbox.glyphs) {
        auto entry = ensureGlyphForm(hFont, g.glyphId, gbox.font->hersheyBold);
        if (entry.objId == 0) {
            curX += g.xAdvance;
            continue;
        }

        qreal gx = curX + g.xOffset;
        qreal gy = y - g.yOffset;

        // Superscript/subscript adjustment
        if (gbox.style.superscript)
            gy += fontSize * 0.35;
        else if (gbox.style.subscript)
            gy -= fontSize * 0.15;

        stream += "q\n";

        if (gbox.font->hersheyItalic) {
            // Scale + italic skew + translate
            // Matrix: [sx 0 sx*tan(12deg) sy tx ty]
            stream += pdfCoord(scale) + " 0 "
                    + pdfCoord(scale * 0.2126) + " " + pdfCoord(scale)
                    + " " + pdfCoord(gx) + " " + pdfCoord(gy) + " cm\n";
        } else {
            // Scale + translate only
            stream += pdfCoord(scale) + " 0 0 " + pdfCoord(scale)
                    + " " + pdfCoord(gx) + " " + pdfCoord(gy) + " cm\n";
        }

        stream += "/" + entry.pdfName + " Do\n";
        stream += "Q\n";

        curX += g.xAdvance;
    }
```

Also remove the old `q`/`Q` wrapper, line-cap/join/width setup, and stroke color that surrounded the old loop (lines 1143-1148 and 1206). The `q`/`Q` is now per-glyph, and stroke styling lives inside the Form XObject.

**Keep unchanged:** The background rect rendering (lines 1128-1136), underline (1208-1217), strikethrough (1220-1228), and link rect collection (1231-1234).

The color must now be set per-glyph before the `Do`. Add the stroke color inside the per-glyph `q`/`Q` block, after the `stream += "q\n";` line:

```cpp
        stream += colorOperator(gbox.style.foreground, false); // stroke color
```

**Step 2: Build and verify**

Run: `cmake --build build --target Penelope 2>&1 | tail -20`
Expected: Compiles

**Step 3: Commit**

```
feat: rewrite renderHersheyGlyphBox to use XObject Form Do
```

---

### Task 5: Update trailing hyphen rendering to use XObject Do

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp` (inside `renderLineBox`, lines 825-851)

**Step 1: Replace inline Hershey hyphen strokes with XObject Do**

Replace the Hershey hyphen rendering block (lines 826-851):

```cpp
            if (lastGbox.font->isHershey && lastGbox.font->hersheyFont) {
                HersheyFont *hFont = lastGbox.font->hersheyFont;
                auto entry = ensureGlyphForm(hFont, uint(U'-'), lastGbox.font->hersheyBold);
                if (entry.objId != 0) {
                    qreal scale = lastGbox.fontSize / hFont->unitsPerEm();
                    stream += "q\n";
                    stream += colorOperator(lastGbox.style.foreground, false);
                    if (lastGbox.font->hersheyItalic) {
                        stream += pdfCoord(scale) + " 0 "
                                + pdfCoord(scale * 0.2126) + " " + pdfCoord(scale)
                                + " " + pdfCoord(x) + " " + pdfCoord(baselineY) + " cm\n";
                    } else {
                        stream += pdfCoord(scale) + " 0 0 " + pdfCoord(scale)
                                + " " + pdfCoord(x) + " " + pdfCoord(baselineY) + " cm\n";
                    }
                    stream += "/" + entry.pdfName + " Do\n";
                    stream += "Q\n";
                }
```

**Step 2: Build and verify**

Run: `cmake --build build --target Penelope 2>&1 | tail -20`
Expected: Compiles

**Step 3: Commit**

```
feat: use XObject Do for trailing soft-hyphen rendering
```

---

### Task 6: Verify stroke width scaling

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp` (inside `ensureGlyphForm`)

**Context:** The old code computed stroke width as `0.02 * fontSize` in PDF points. The new XObject stores stroke width in glyph units (unscaled). When the call site applies `scale = fontSize / unitsPerEm` via the `cm` matrix, all coordinates AND line widths are multiplied by `scale`. So the XObject stroke width must produce the same visual result after scaling.

Old: `strokeWidth_points = 0.02 * fontSize`
New: `strokeWidth_glyphUnits * scale = strokeWidth_glyphUnits * (fontSize / unitsPerEm)`

Solving: `strokeWidth_glyphUnits = 0.02 * unitsPerEm`

**Step 1: Fix the stroke width calculation in ensureGlyphForm**

Replace the stroke width lines:
```cpp
    qreal strokeWidth = 0.5; // base stroke in glyph units
```
with:
```cpp
    qreal strokeWidth = 0.02 * font->unitsPerEm();
```

**Step 2: Build and do a visual test**

Run: `cmake --build build --target Penelope 2>&1 | tail -20`
Then manually export a PDF and compare stroke thickness with the old rendering. Glyphs should look identical.

**Step 3: Commit**

```
fix: correct XObject stroke width to match old fontSize-based calculation
```

---

### Task 7: Visual verification and cleanup

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp` (if any fixes needed)

**Step 1: Build the full app**

Run: `cmake --build build --target Penelope 2>&1 | tail -20`
Expected: Clean build, no warnings

**Step 2: Visual comparison test**

Export a test PDF with Hershey fonts and verify:
- [ ] Glyphs render at correct positions (same as before)
- [ ] Bold text is thicker
- [ ] Italic text is skewed
- [ ] Colors are correct (links blue, code text colored)
- [ ] Background rects (inline code) still render
- [ ] Underline and strikethrough still work
- [ ] Soft hyphens render correctly
- [ ] Markdown copy mode: invisible text layer works, ActualText copy-paste works
- [ ] Justified text spacing is unchanged
- [ ] File size is smaller than before (check with `ls -la`)

**Step 3: Check PDF structure**

Open the PDF in a text editor or use `qpdf --show-object` to verify:
- Form XObjects appear as `/Type /XObject /Subtype /Form` objects
- Resource dict lists `HG0`, `HG1`, etc. under `/XObject`
- Content streams contain `cm` + `/HGn Do` instead of inline `m`/`l`/`S`

**Step 4: Remove debug qDebug statements if desired**

The previous commit added many `qDebug()` calls in linebreaker.cpp, layoutengine.cpp, and pdfgenerator.cpp. These can be cleaned up in a separate commit if desired.

**Step 5: Commit any fixes**

```
fix: XObject glyph rendering visual fixes
```
