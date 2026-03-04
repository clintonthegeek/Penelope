# Web View Mode — Design Document

**Date:** 2026-02-21
**Status:** Approved

## Overview

Add a dynamic "web" view mode to Penelope — a continuous, reflow-based text view
with no page boundaries, no headers/footers, word-wrapped to the viewport width. All
footnotes appear at the document bottom. Tables and code blocks wrap to fit the viewport.
The view uses the same HarfBuzz shaping and Knuth-Plass line breaking as the print view,
rendered directly via QPainter instead of going through PDF generation.

## Architecture

```
Content::Document (shared)
  → Layout::Engine
      ├─ layout()           → LayoutResult (pages)    → PdfGenerator → Poppler → PdfPageItem (print)
      └─ layoutContinuous() → ContinuousLayoutResult   → WebViewItem::paint() via QPainter  (web)
```

The web view branches off after the Layout::Engine, sharing all upstream processing
(markdown parsing, content building, style resolution, font loading, text shaping).
It skips PDF generation, Poppler rasterization, and the render cache entirely.

### New Components

- **`Layout::Engine::layoutContinuous()`** — reuses all block layout methods, stacks
  elements vertically without page breaks or splitting
- **`ContinuousLayoutResult`** — flat list of positioned `PageElement`s with total height
- **`WebViewItem`** — single `QGraphicsItem` that paints the entire box tree
- **`WebViewRenderer`** — stateless helper translating each box type into QPainter calls

### Modified Components

- **`DocumentView`** — gains `RenderMode` (Print/Web); in web mode, scene contains one
  `WebViewItem` instead of N `PdfPageItem`s
- **`MainWindow`** — adds Print/Web toggle to toolbar; disables page arrangement in web mode

### Untouched Components

- PDF pipeline (PdfGenerator, Poppler, RenderCache, PdfPageItem)
- Print/Export (always uses paginated pipeline)
- Themes, styles, fonts (shared between views)
- Source editor mode

## Layout Engine — `layoutContinuous()`

### New Types

```cpp
struct ContinuousLayoutResult {
    QList<PageElement> elements;       // each with absolute y position
    qreal totalHeight = 0;
    qreal contentWidth = 0;
    QList<SourceMapEntry> sourceMap;   // pageNumber always 0, absolute rects
    QList<CodeBlockRegion> codeBlockRegions;
};
```

### Implementation

1. Runs the same block layout loop as `layout()` — `layoutParagraph()`, `layoutHeading()`,
   `layoutTable()`, `layoutCodeBlock()`, etc. — all taking `availWidth`
2. Instead of `assignToPages()`, walks the element list accumulating y positions:
   `y += spaceBefore; element.y = y; y += height + spaceAfter`
3. No block splitting, orphan/widow protection, or keep-with-next — these are page concerns
4. Source map entries get `pageNumber = 0` and absolute rect positions
5. Footnotes forced to endnote mode (all at document bottom) regardless of style setting

## WebViewItem & WebViewRenderer — QPainter Rendering

### Glyph Rendering

Uses `QRawFont` (from same font files as FontManager) + `QGlyphRun` to render
HarfBuzz-shaped glyphs at exact positions. This preserves all OpenType features,
kerning, and ligatures from the existing typography pipeline.

### Rendering Operations by Box Type

| Box Type | QPainter Operations |
|---|---|
| Paragraph/heading | Optional `fillRect` background, iterate lines |
| Code block | `fillRect` background + optional `drawRect` border, iterate lines |
| Blockquote | `drawLine` for left border (gray, 2pt), iterate lines |
| Horizontal rule | `drawLine` centered, gray, 0.5pt |
| Image | `drawImage` scaled to imageWidth × imageHeight |
| Line glyphs | `QGlyphRun` + `drawGlyphRun`, manual justification spacing |
| Inline backgrounds | `fillRect` per glyph box (inline code) |
| Underline/strikethrough | `drawLine` at appropriate y-offsets |
| Superscript/subscript | y-offset shifts on glyph positions |
| Checkboxes | `drawRoundedRect` + optional `drawPolyline` checkmark |
| Hershey glyphs | `drawPolyline` per stroke, shear `QTransform` for italic |
| Table | Cell backgrounds via `fillRect`, text via line rendering, grid via `drawLine` |
| Footnote section | Partial-width `drawLine` separator, then footnote lines |

### Paint Clipping

`paint()` checks the exposed rect and only renders elements overlapping it. Binary
search on `element.y` finds the visible range. This keeps painting fast for long documents.

### Link Hit Rects

Link rects are collected during rendering and stored for DocumentView's mouse
hover/click handling.

## DocumentView Integration

### Render Mode

```cpp
enum RenderMode { PrintMode, WebMode };
```

Orthogonal to existing `ViewMode` (Continuous, SinglePage, etc.). In web mode,
`ViewMode` is ignored — the view is always continuous scroll.

### Key Methods

- `setRenderMode(RenderMode)` — switches between print and web
- `setWebContent(ContinuousLayoutResult &&result)` — replaces scene with single WebViewItem
- WebViewItem placed at `(kSceneMargin, kSceneMargin)` centered in scene

### Resize Handling — Debounced Relayout

During resize drag, the scene stays at its current width. After 200ms idle, a full
relayout fires at the new viewport width. Scroll position is preserved by recording
the topmost visible heading index before relayout and restoring it after.

```
User drags window wider...
  → Scene stays at old width (gap visible)
  → 200ms after drag stops...
  → Full relayout at new width
  → Scroll position preserved by heading anchor
```

### Zoom Behavior

Zoom scales text size (like browser Ctrl+/-):

1. Immediately apply `QGraphicsView::scale()` for instant visual feedback
2. Debounced (200ms): relayout at `effectiveWidth = viewportWidth / zoomFactor`
3. After relayout, content has correct line breaks at the zoomed scale

## MainWindow Integration

### Toolbar Toggle

A `QActionGroup` with two `QAction`s (Print / Web), displayed as toggle buttons in
the toolbar. Persisted in `PenelopeSettings`.

### Mode Switching

**To web mode:**
1. Disable "Page Arrangement" submenu (6 view modes grayed out)
2. `rebuildCurrentDocument()` uses web pipeline:
   ContentBuilder → `layoutContinuous()` → `setWebContent()`

**To print mode:**
1. Re-enable page arrangement submenu
2. Full rebuild through PDF pipeline

### Shared Features

- TOC sidebar and scroll-sync work in both modes (WebViewItem provides heading positions)
- File browser unchanged
- Theme picker applies to both views

## Content Behavior in Web Mode

| Content | Behavior |
|---|---|
| Body text | Word-wraps to viewport width |
| Headings | Full viewport width |
| Tables | Fit viewport width, columns shrink proportionally |
| Code blocks | Word-wrap to viewport width |
| Images | Scale to fit content width |
| Footnotes | All at document bottom (endnote mode forced) |
| Headers/footers | Hidden |
| Page boundaries | None |
| Links | Clickable (collected as hit rects) |

## Out of Scope

- **Text selection** in web view (can use source mode; add later)
- **Search/find** in web view (requires text position index; add later)
- **Per-element horizontal scrolling** (everything wraps to viewport)
- **Different themes per mode** (same theme applies to both)
- **Smooth resize animation** (debounced relayout only)
- **Layout caching** (regenerate on every relayout; cache if profiling warrants)

## Performance Considerations

The web view pipeline is faster than print because it skips PDF generation and
Poppler rasterization. The critical path on resize is:

```
layoutContinuous()  →  WebViewItem::setLayoutResult()  →  scene.update()
```

Where `layoutContinuous()` is dominated by HarfBuzz shaping and Knuth-Plass line
breaking. For typical documents this should complete in <100ms, making the 200ms
debounce feel responsive.
