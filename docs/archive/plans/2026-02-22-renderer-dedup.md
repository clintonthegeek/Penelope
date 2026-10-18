# Renderer Pipeline Deduplication Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate ~350 lines of duplicated rendering logic between PdfGenerator and WebViewRenderer by extracting a shared BoxTreeRenderer base class with virtual drawing primitives.

**Architecture:** BoxTreeRenderer base class owns box-tree traversal and math (justification, alignment, glyph positioning). QtBoxRenderer (QPainter) and PdfBoxRenderer (PDF operators) implement virtual drawing primitives. PdfBoxRenderer overrides renderLineBox() for ActualText. See `docs/plans/2026-02-22-renderer-dedup-design.md` for full design.

**Tech Stack:** C++20, Qt6 (QPainter, QRawFont, QGlyphRun), FreeType, PDF operators, CMake.

---

### Task 1: Create BoxTreeRenderer base class with drawing primitive interface

**Files:**
- Create: `src/render/boxtreerenderer.h`
- Create: `src/render/boxtreerenderer.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create the header with virtual interface**

Create `src/render/boxtreerenderer.h`. The base class declares:

- Pure virtual drawing primitives: `drawRect`, `drawLine`, `drawGlyphs`,
  `drawHersheyStrokes`, `drawImage`, `pushState`, `popState`, `collectLink`,
  `drawRoundedRect`, `drawCheckmark`, `drawPolyline`
- Virtual render methods (will be populated in Task 2): `renderElement`,
  `renderBlockBox`, `renderTableBox`, `renderFootnoteSectionBox`,
  `renderLineBox`, `renderGlyphBox`, `renderHersheyGlyphBox`,
  `renderGlyphDecorations`, `renderCheckbox`, `renderImageBlock`
- Protected constructor taking `FontManager*`
- Virtual destructor

The drawing primitives work in layout-engine coordinates (top-down). Each
backend transforms to its native coordinate system inside the primitive.

The `drawGlyphs` primitive takes `FontFace*`, `fontSize`, glyph IDs + positions,
and a foreground color. The Qt backend maps this to QGlyphRun; the PDF backend
maps it to BT/Tf/Tm/Tj/ET operators.

The `drawHersheyStrokes` primitive takes a list of polyline strokes, a
QTransform, foreground color, and stroke width. The Qt backend maps this to
QPainter::drawPolyline with transform; the PDF backend maps to cm + form XObject
Do.

Note: The `renderLineBox` signature includes `availWidth` for justification.
The PDF backend override will call `computeGlyphXPositions()` (a protected
helper) before its ActualText logic, then call the base for visible rendering.

Add a protected helper: `computeGlyphXPositions(line, originX, availWidth) ->
QList<qreal>` that encapsulates the justification x-position math (currently
duplicated in both renderers).

Add a protected helper: `computeJustification(line, availWidth) -> struct
{doJustify, extraPerGap, extraPerChar}` that encapsulates the justification
parameter computation.

```cpp
// Key types in the interface:
struct GlyphRenderInfo {
    QVector<quint32> glyphIds;
    QVector<QPointF> positions; // relative to (x, baselineY)
};
```

**Step 2: Create empty .cpp with constructor/destructor**

Create `src/render/boxtreerenderer.cpp` with just the constructor and destructor
implementations. The traversal methods will be added in Task 2.

**Step 3: Register in CMakeLists.txt**

Add `render/boxtreerenderer.cpp`, `render/boxtreerenderer.h` to the
`PenelopeCore` STATIC library sources. Add
`${CMAKE_CURRENT_SOURCE_DIR}/render` to the `target_include_directories` PUBLIC
list.

**Step 4: Build to verify compilation**

Run: `cmake --build build --target PenelopeCore`
Expected: Clean compilation, no errors.

**Step 5: Commit**

```
feat(render): add BoxTreeRenderer base class with drawing primitive interface
```

---

### Task 2: Move shared traversal logic into BoxTreeRenderer

**Files:**
- Modify: `src/render/boxtreerenderer.h`
- Modify: `src/render/boxtreerenderer.cpp`

This is the core task. Move the traversal and math from
`webviewrenderer.cpp` into `boxtreerenderer.cpp`, replacing QPainter calls with
virtual primitive calls.

**Step 1: Implement `renderElement()`**

Port the `std::visit` dispatch from `webviewitem.cpp:85-93`. This routes
`PageElement` variants to the appropriate render method.

```cpp
void BoxTreeRenderer::renderElement(const Layout::PageElement &elem,
                                     qreal originX, qreal originY)
{
    std::visit([&](const auto &e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, Layout::BlockBox>)
            renderBlockBox(e, originX, originY);
        else if constexpr (std::is_same_v<T, Layout::TableBox>)
            renderTableBox(e, originX, originY);
        else if constexpr (std::is_same_v<T, Layout::FootnoteSectionBox>)
            renderFootnoteSectionBox(e, originX, originY);
    }, elem);
}
```

**Step 2: Implement `computeJustification()` and `computeGlyphXPositions()`**

Extract the justification logic from `webviewrenderer.cpp:221-253` into
`computeJustification()`. Extract the x-position loop from
`webviewrenderer.cpp:257-290` into `computeGlyphXPositions()`.

The justification threshold (`maxJustifyGap`) should be a configurable member
(`m_maxJustifyGap`, defaulting to 20.0). PdfBoxRenderer can override via setter.

**Step 3: Implement `renderBlockBox()`**

Port from `webviewrenderer.cpp:35-93`. Replace:
- `painter->drawRect(bgRect)` with `drawRect(bgRect, fill, stroke, strokeWidth)`
- `painter->drawLine(...)` with `drawLine(p1, p2, color, width)`
- The line iteration loop stays as-is, calling `renderLineBox()`

**Step 4: Implement `renderTableBox()`**

Port from `webviewrenderer.cpp:98-185`. Same 3-pass structure (backgrounds,
content, borders). Replace QPainter calls with virtual primitives.

**Step 5: Implement `renderFootnoteSectionBox()`**

Port from `webviewrenderer.cpp:189-211`.

**Step 6: Implement `renderLineBox()`**

Port from `webviewrenderer.cpp:215-336`. This is the most complex method:
- Uses `computeJustification()` and `computeGlyphXPositions()`
- Iterates glyph boxes, calling `renderGlyphBox()` at computed positions
- Handles trailing soft-hyphen rendering

The trailing soft-hyphen logic needs to call `drawGlyphs()` or
`drawHersheyStrokes()` depending on font type.

**Step 7: Implement `renderGlyphBox()`**

Port from `webviewrenderer.cpp:341-399`. Dispatches to
`renderHersheyGlyphBox()` for Hershey fonts, `renderCheckbox()` for checkboxes.
For TTF glyphs:
- Draws inline background via `drawRect()`
- Computes glyph positions (curX + xOffset, superscript/subscript adjustment)
- Calls `drawGlyphs(font, fontSize, glyphIds, positions, color)`
- Calls `renderGlyphDecorations()`

**Step 8: Implement `renderHersheyGlyphBox()`**

Port from `webviewrenderer.cpp:404-468`. For each glyph:
- Computes transform (scale, italic skew, translation)
- Calls `drawHersheyStrokes(strokes, transform, color, strokeWidth)`

**Step 9: Implement `renderGlyphDecorations()`**

Port from `webviewrenderer.cpp:473-497`. Calls `drawLine()` for underline and
strikethrough, `collectLink()` for link hrefs.

**Step 10: Implement `renderCheckbox()`**

Port from `webviewrenderer.cpp:501-533`. Calls `drawRoundedRect()`,
`drawCheckmark()`, `drawPolyline()`.

**Step 11: Implement `renderImageBlock()`**

Port from `webviewrenderer.cpp:537-544`. Calls `drawImage()`.

**Step 12: Build to verify compilation**

Run: `cmake --build build --target PenelopeCore`
Expected: Clean compilation (no concrete subclass yet, but the base builds).

**Step 13: Commit**

```
feat(render): implement shared traversal logic in BoxTreeRenderer
```

---

### Task 3: Create QtBoxRenderer (QPainter backend)

**Files:**
- Create: `src/canvas/qtboxrenderer.h`
- Create: `src/canvas/qtboxrenderer.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create header**

`QtBoxRenderer` subclasses `BoxTreeRenderer`. It:
- Takes `FontManager*` and `QPainter*` in constructor (painter set per-paint call)
- Implements all virtual drawing primitives via QPainter
- Manages `QHash<QPair<FontFace*, int>, QRawFont> m_rawFontCache`
- Stores `QList<LinkHitRect> m_linkHitRects`
- Provides `setPainter(QPainter*)`, `linkHitRects()`, `clearLinkHitRects()`

**Step 2: Implement drawing primitives**

Map each primitive to QPainter calls. These are mostly 5-10 lines each:

- `drawRect`: `painter->setPen/setBrush/drawRect`
- `drawLine`: `painter->setPen/drawLine`
- `drawGlyphs`: build QGlyphRun from rawFontFor(), `painter->drawGlyphRun`
  Port the QRawFont cache from `webviewrenderer.cpp:20-31`.
- `drawHersheyStrokes`: `painter->setPen/setTransform/drawPolyline`
- `drawImage`: `painter->drawImage`
- `pushState/popState`: `painter->save/restore`
- `collectLink`: append to `m_linkHitRects`
- `drawRoundedRect`: `painter->drawRoundedRect`
- `drawCheckmark`: `painter->drawPolyline` (for the check shape)

**Step 3: Register in CMakeLists.txt**

Add `canvas/qtboxrenderer.cpp` and `canvas/qtboxrenderer.h`.

**Step 4: Build to verify compilation**

Run: `cmake --build build --target PenelopeCore`
Expected: Clean compilation.

**Step 5: Commit**

```
feat(canvas): add QtBoxRenderer QPainter backend
```

---

### Task 4: Wire QtBoxRenderer into WebViewItem, replace WebViewRenderer

**Files:**
- Modify: `src/canvas/webviewitem.h`
- Modify: `src/canvas/webviewitem.cpp`

**Step 1: Replace WebViewRenderer with QtBoxRenderer**

In `webviewitem.h`:
- Replace `#include "webviewrenderer.h"` with `#include "qtboxrenderer.h"`
- Replace `WebViewRenderer m_renderer;` with `QtBoxRenderer m_renderer;`
- Update `linkHitRects()` accessor if needed

In `webviewitem.cpp`:
- Replace the `std::visit` rendering dispatch in `paint()` with a call to
  `m_renderer.renderElement()` for each visible element. The renderer's
  `setPainter()` is called at the start of paint, then each element is rendered
  via the base class traversal.

The `paint()` method becomes:

```cpp
void WebViewItem::paint(QPainter *painter, ...) {
    painter->fillRect(exposed, m_pageBackground);
    m_renderer.clearLinkHitRects();
    m_renderer.setPainter(painter);
    int startIdx = firstVisibleElement(exposed.top());
    for (int i = startIdx; i < m_result.elements.size(); ++i) {
        // ... visibility check unchanged ...
        m_renderer.renderElement(m_result.elements[i], 0, 0);
    }
}
```

**Step 2: Build and test visually**

Run: `cmake --build build && ./build/src/Penelope`
Open a markdown file in web view mode. Verify:
- Text renders correctly (fonts, sizes, colors)
- Justification alignment matches previous behavior
- Code blocks have backgrounds and borders
- Tables render with grid lines
- Hershey fonts render correctly (if a Hershey theme is active)
- Links are clickable
- Images display correctly
- Checkboxes display correctly
- Footnotes render with separator line

**Step 3: Commit**

```
refactor(canvas): replace WebViewRenderer with QtBoxRenderer in WebViewItem
```

---

### Task 5: Create PdfBoxRenderer (PDF operator backend)

**Files:**
- Create: `src/pdf/pdfboxrenderer.h`
- Create: `src/pdf/pdfboxrenderer.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create header**

`PdfBoxRenderer` subclasses `BoxTreeRenderer`. It:
- Takes `FontManager*`, content-area origin (`originX`, `contentTopY`), and
  `pageHeight` for Y-flip
- Provides `setStream(QByteArray*)` to direct output
- Implements drawing primitives via PDF operators
- Holds references/pointers needed for PDF rendering:
  - A function/callback for `pdfFontName(FontFace*)` (provided by PdfGenerator)
  - A function/callback for `ensureGlyphForm(...)` (for Hershey/XObject glyphs)
  - A function/callback for `markGlyphUsed(FontFace*, uint)` (font subsetting)
  - Export options reference (markdownCopy, xobjectGlyphs)
- Overrides `renderLineBox()` to inject ActualText markdown copy logic
- Overrides `renderGlyphBox()` to dispatch between the 4 PDF glyph modes
  (CIDFont, path, XObject, Hershey forms)
- Stores link annotations (collected via `collectLink()`)

The Y-flip is done in each primitive:

```cpp
qreal pdfY(qreal layoutY) const { return m_contentTopY - layoutY; }
```

**Step 2: Implement drawing primitives**

Each primitive writes PDF operators to `*m_stream`:
- `drawRect`: `q / rg / re f / RG / w / re S / Q`
- `drawLine`: `q / RG / w / m l S / Q`
- `drawGlyphs`: dispatch through the 4 rendering modes (port from
  `pdfgenerator.cpp:589-1203` — `dispatchGlyphRendering`, `renderGlyphBox`,
  `renderGlyphBoxAsPath`, `renderTtfGlyphBoxAsXObject`, `renderHersheyGlyphBox`)
- `drawHersheyStrokes`: use form XObject via `ensureGlyphForm` callback
- `drawImage`: `q / cm / Do / Q` (image must be pre-registered)
- `pushState/popState`: `q` / `Q`
- `collectLink`: call through to PdfGenerator's link annotation collection
- `drawRoundedRect`: PDF Bezier curves for rounded corners
- `drawCheckmark`: PDF line paths

**Step 3: Implement `renderLineBox()` override**

Port the markdown copy ActualText logic from `pdfgenerator.cpp:631-917`.

Structure:
1. If markdown copy mode, compute glyph x-positions via
   `computeGlyphXPositions()`
2. Emit BDC ActualText span with line text
3. Emit invisible text overlay (BT/3 Tr/Tm/Tj/ET)
4. Emit EMC
5. Call `BoxTreeRenderer::renderLineBox()` for visible rendering (or render
   glyphs directly using x-positions if needed for the markdown path)
6. Handle trailing soft-hyphen rendering

When markdown copy is disabled, just call `BoxTreeRenderer::renderLineBox()`.

**Step 4: Register in CMakeLists.txt**

Add `pdf/pdfboxrenderer.cpp` and `pdf/pdfboxrenderer.h`.

**Step 5: Build to verify compilation**

Run: `cmake --build build --target PenelopeCore`
Expected: Clean compilation.

**Step 6: Commit**

```
feat(pdf): add PdfBoxRenderer PDF operator backend
```

---

### Task 6: Wire PdfBoxRenderer into PdfGenerator, remove duplicated rendering methods

**Files:**
- Modify: `src/pdf/pdfgenerator.h`
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Refactor PdfGenerator::renderPage() to use PdfBoxRenderer**

Replace the current `renderPage()` which calls `renderBlockBox()`,
`renderTableBox()`, `renderFootnoteSectionBox()` directly. Instead:

```cpp
QByteArray PdfGenerator::renderPage(const Layout::Page &page, ...) {
    QByteArray stream;
    // ... margin calculations unchanged ...
    renderHeaderFooter(stream, ...);

    PdfBoxRenderer renderer(m_fontManager, originX, contentTopY, pageHeight);
    renderer.setStream(&stream);
    renderer.setMaxJustifyGap(m_maxJustifyGap);
    // Wire up callbacks for font/glyph management
    renderer.setFontNameCallback([this](FontFace *f) { return pdfFontName(f); });
    renderer.setGlyphFormCallback([this](...) { return ensureGlyphForm(...); });
    renderer.setMarkGlyphUsedCallback([this](FontFace *f, uint g) {
        m_fontManager->markGlyphUsed(f, g);
    });
    renderer.setExportOptions(m_exportOptions);

    for (const auto &elem : page.elements)
        renderer.renderElement(elem, originX, contentTopY);

    // Collect link annotations from renderer
    for (const auto &link : renderer.linkAnnotations())
        m_pageAnnotations[m_currentPageIndex].append(link);

    return stream;
}
```

**Step 2: Remove duplicated render methods from PdfGenerator**

Remove from `pdfgenerator.h` and `pdfgenerator.cpp`:
- `renderBlockBox()` (lines 386-528)
- `renderTableBox()` (lines 1321-1413)
- `renderFootnoteSectionBox()` (lines 1417-1449)
- `renderLineBox()` (lines 631-917)
- `renderGlyphBox()` (lines 986-1034)
- `renderGlyphBoxAsPath()` (lines 1036-1092)
- `renderTtfGlyphBoxAsXObject()` (lines 1094-1141)
- `renderHersheyGlyphBox()` (lines 1143-1203)
- `renderGlyphDecorations()` (lines 603-629)
- `renderCheckbox()` (lines 919-984)
- `dispatchGlyphRendering()` (lines 589-601)
- `renderImageBlock()` (lines 1603-1625)
- `collectLinkRect()` (lines 1629-1641)

Keep in PdfGenerator:
- `generate()`, `generateToFile()` — PDF document structure
- `renderPage()` — now delegates to PdfBoxRenderer
- `renderHeaderFooter()` — PDF-specific
- Font embedding: `embedFonts`, `writeCidFont`, `buildToUnicodeCMap`,
  `ensureFontRegistered`, `pdfFontName`
- Image embedding: `embedImages`, `ensureImageRegistered`
- Glyph forms: `ensureGlyphForm` (still in PdfGenerator, called by
  PdfBoxRenderer via callback)
- Bookmarks: `writeOutlines`
- `toUtf16BeHex`, `colorOperator`, `pdfCoord` — keep as utilities
  (PdfBoxRenderer may also need `colorOperator` and `pdfCoord`; consider making
  them static or moving to a shared PDF utility header)

Also remove from `pdfgenerator.h`:
- `LinkAnnotation` struct and `m_pageAnnotations` — these can move into
  PdfBoxRenderer or stay if PdfGenerator still assembles annotations into the
  PDF page objects

**Step 3: Build and test**

Run: `cmake --build build && ./build/src/Penelope`
Test PDF generation:
- Open a file, render in paginated mode
- Export to PDF
- Verify text, images, code blocks, tables, footnotes render correctly
- Verify links are clickable in the PDF
- Verify markdown copy works (select text in PDF viewer, paste)
- Verify bookmarks work
- Verify Hershey fonts render correctly

**Step 4: Commit**

```
refactor(pdf): delegate page rendering to PdfBoxRenderer, remove duplicated methods
```

---

### Task 7: Remove old WebViewRenderer files

**Files:**
- Delete: `src/canvas/webviewrenderer.h`
- Delete: `src/canvas/webviewrenderer.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Remove WebViewRenderer from CMakeLists.txt**

Remove `canvas/webviewrenderer.cpp` and `canvas/webviewrenderer.h` from the
source list.

**Step 2: Delete the files**

```bash
git rm src/canvas/webviewrenderer.h src/canvas/webviewrenderer.cpp
```

**Step 3: Verify no remaining references**

```bash
grep -r "webviewrenderer" src/ --include='*.cpp' --include='*.h'
grep -r "WebViewRenderer" src/ --include='*.cpp' --include='*.h'
```

Expected: No matches.

**Step 4: Build full project**

Run: `cmake --build build`
Expected: Clean compilation.

**Step 5: Final visual verification**

Run: `./build/src/Penelope`
- Test web view rendering (open file, verify all element types)
- Test PDF export (File > Export PDF, verify output)
- Test paginated print view
- Test with Hershey font theme
- Test with markdown copy enabled in PDF export options

**Step 6: Commit**

```
refactor(canvas): remove old WebViewRenderer (replaced by QtBoxRenderer)
```

---

### Notes for Implementation

**PDF coordinate utilities:** `pdfCoord()` and `colorOperator()` are used by
both PdfGenerator and PdfBoxRenderer. Options:
1. Make them `static` public methods on PdfGenerator (PdfBoxRenderer includes pdfgenerator.h)
2. Move them to a small `src/pdf/pdfutils.h` header-only utility
3. Move them to PdfBoxRenderer and have PdfGenerator call them

Option 1 is simplest. Option 2 is cleanest if the header already exists. Decide
during implementation.

**FreeType outline decomposition:** The `OutlineCtx` struct and FreeType callback
functions (`outlineMoveTo`, `outlineLineTo`, etc.) in `pdfgenerator.cpp:532-587`
are used by `renderGlyphBoxAsPath()` and `ensureGlyphForm()`. Since both live in
the PDF layer, they can stay as anonymous-namespace helpers in either
`pdfboxrenderer.cpp` or `pdfgenerator.cpp` (wherever the code that uses them
ends up). `ensureGlyphForm()` stays in PdfGenerator, so the outline helpers
should stay there too. PdfBoxRenderer's path rendering mode can call through to
PdfGenerator via the callback.

**Testing strategy:** This is a rendering refactor with no behavioral change.
Visual verification is the primary test. Render the same document before and
after, compare output. The PDF output should be byte-identical (or very close)
for the same input.

**Rollback safety:** Each task produces a working build. If any task fails
visually, the previous commit is a safe rollback point.
