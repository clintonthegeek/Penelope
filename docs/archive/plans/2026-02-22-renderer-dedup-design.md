# Renderer Pipeline Deduplication Design

## Problem

The PDF and web view rendering pipelines share identical traversal logic,
justification math, glyph positioning, and decoration rendering, but implement
them independently. ~350-400 lines are duplicated. Every new block type (equation,
diagram, etc.) must be implemented twice with parallel structure. Adding a new
output backend would require copying the traversal a third time.

## Approach: Abstract Renderer with Shared Traversal

A `BoxTreeRenderer` base class owns the box-tree traversal and shared math.
It calls virtual drawing primitives that each backend implements.

## Class Hierarchy

```
BoxTreeRenderer (src/render/boxtreerenderer.h/.cpp)
 |
 |  Shared traversal:
 |   renderElement()           -- PageElement variant dispatch
 |   renderBlockBox()          -- background, border, hrule, blockquote, line loop
 |   renderTableBox()          -- 3-pass: backgrounds, content, borders
 |   renderFootnoteSectionBox()-- separator line, footnote iteration
 |   renderLineBox()           -- justification, alignment, glyph iteration
 |   renderGlyphBox()          -- TTF vs Hershey dispatch, position math
 |   renderGlyphDecorations()  -- underline, strikethrough
 |   renderCheckbox()          -- vector checkbox drawing
 |   renderImageBlock()        -- image placement
 |
 |  Virtual drawing primitives:
 |   drawRect(rect, fill, stroke, strokeWidth)
 |   drawLine(p1, p2, color, width)
 |   drawGlyphs(font, fontSize, glyphs[], positions[], color)
 |   drawHersheyStrokes(strokes[], transform, color, strokeWidth)
 |   drawImage(image, rect)
 |   pushState() / popState()
 |   collectLink(rect, href)
 |
 +-- QtBoxRenderer (src/canvas/qtboxrenderer.h/.cpp)
 |    Implements primitives via QPainter
 |    Manages QRawFont cache
 |    Stores LinkHitRect list
 |
 +-- PdfBoxRenderer (src/pdf/pdfboxrenderer.h/.cpp)
      Implements primitives via PDF operators -> QByteArray
      Overrides renderLineBox() for ActualText markdown copy
      Y-flip in coordinate primitives
```

## Coordinate Convention

The shared traversal works in layout-engine coordinates: top-down, origin at
the top-left of the content area. This matches `BlockBox::y`, `LineBox::y`, etc.

- **Qt backend**: coordinates pass through unchanged (QPainter is top-down).
- **PDF backend**: each drawing primitive applies the Y-flip internally using
  `pdfY = contentTopY - layoutY`. The flip is entirely contained in the
  primitive implementations, invisible to the shared traversal.

## Markdown Copy (ActualText) Strategy

PDF's ActualText is handled via `PdfBoxRenderer::renderLineBox()` override:

1. Override calls base `computeGlyphPositions()` to get x-positions.
2. Override emits BDC/EMC ActualText spans and invisible text overlay.
3. Override calls `BoxTreeRenderer::renderLineBox()` for visible glyph rendering.

This keeps the markdown copy logic as a PDF-only hook, cleanly separated from
the shared traversal.

## Extensibility: Adding New Block Types

To add a new block type (e.g. EquationBox):

1. Add the box struct to `layoutengine.h`, include in `PageElement` variant.
2. Add `renderEquationBox()` to `BoxTreeRenderer` with shared traversal logic.
3. Add new drawing primitives if needed (e.g. `drawPath()` for curves).
4. Each backend implements the new primitives.
5. If a backend needs special behavior, it overrides `renderEquationBox()`.

The `renderElement()` dispatch handles routing via `std::visit`.

## Extensibility: Adding New Backends

To add a new output backend (e.g. SVG):

1. Subclass `BoxTreeRenderer`.
2. Implement the virtual drawing primitives for the new format.
3. Override higher-level methods only if the backend has special needs.

All traversal, justification, alignment, and glyph positioning come for free.

## What Stays Backend-Specific

### PdfGenerator retains:
- `generate()` / `generateToFile()` -- PDF document assembly
- Font embedding (CIDFont, subsetting, ToUnicode CMap)
- Image embedding (DCTDecode/FlateDecode)
- PDF bookmarks / outlines
- Header/footer rendering
- Link annotations
- Glyph form XObjects (ensureGlyphForm)
- The 4 glyph rendering modes via drawGlyphs/drawHersheyStrokes

### QtBoxRenderer retains:
- QRawFont cache (face+size -> QRawFont)
- QGlyphRun composition
- Link hit rect collection for mouse interaction

## File Layout

```
src/render/
  boxtreerenderer.h       -- base class declaration
  boxtreerenderer.cpp      -- shared traversal and math (~300-350 lines)

src/canvas/
  qtboxrenderer.h          -- Qt/QPainter backend declaration
  qtboxrenderer.cpp         -- QPainter primitive implementations (~200 lines)
  webviewitem.h/.cpp       -- unchanged (instantiates QtBoxRenderer)

src/pdf/
  pdfboxrenderer.h         -- PDF backend declaration
  pdfboxrenderer.cpp        -- PDF operators + ActualText override (~400 lines)
  pdfgenerator.h/.cpp      -- PDF document structure only (shrinks ~500 lines)
```

## Migration Notes

- `WebViewRenderer` is replaced by `QtBoxRenderer`.
- Rendering methods move out of `PdfGenerator` into `PdfBoxRenderer`.
- `PdfGenerator` delegates to `PdfBoxRenderer` for page content rendering.
- No changes to the layout engine, text shaper, or content model.
