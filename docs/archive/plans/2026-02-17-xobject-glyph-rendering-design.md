# XObject Form Glyph Rendering

## Problem

Penelope's PDF export needs visible glyph art that does NOT participate in
text extraction.  The Markdown-copy feature (Layer 3) places invisible
`3 Tr` text with `ActualText` dictionaries for copy-paste; any visible text
operators (`Tj`, `TJ`) compete with this layer, confusing PDF viewers.

Currently Hershey glyphs are drawn as inline path operators (`m`/`l`/`S`)
repeated for every occurrence.  This is text-silent but wasteful: the same
stroke data is emitted hundreds of times per document.

## Solution

Define each unique glyph as a **PDF Form XObject** (a reusable sub-drawing).
Paint glyphs with `cm` + `Do` instead of inline paths.  No text operators in
the visible layer.

## Architecture

### Rendering Pipeline Position

```
Unicode text
  -> HarfBuzz shaping (TTF) or direct mapping (Hershey)
  -> ShapedGlyph list (glyphId + advances + offsets)
  -> Layout engine (line breaking, justification)
  -> GlyphBox list per line
  -> PDF rendering: ensureGlyphForm(glyphId) -> cm + /HGn Do
```

XObject rendering is fully downstream of shaping.  All OpenType features
(ligatures, old-style numerals, contextual alternates) are resolved by
HarfBuzz before any XObject is created.  The glyphId in the Form key is the
post-shaping glyph index, not the Unicode codepoint.

### Data Model

```cpp
struct GlyphFormKey {
    const FontFace *font;   // font face pointer
    uint glyphId;           // post-shaping glyph ID (== codepoint for Hershey)
    bool bold;              // bold stroke variant (Hershey only)
};

struct GlyphFormEntry {
    Pdf::ObjId objId;       // PDF object ID of the Form XObject
    QByteArray pdfName;     // resource name: "HG0", "HG1", ...
    qreal advanceWidth;     // advance width in glyph units
    qreal bboxWidth;        // BBox width
    qreal bboxHeight;       // BBox height
};
```

Cache in PdfGenerator:
```cpp
QHash<GlyphFormKey, GlyphFormEntry> m_glyphForms;
int m_nextGlyphFormIndex = 0;
```

### Form XObject Structure

```
<objId> 0 obj
<< /Type /XObject
   /Subtype /Form
   /BBox [0 <descent> <advanceWidth> <ascent>]
   /Length <streamLength>
>>
stream
1 J 1 j              % round line cap & join
<lineWidth> w         % stroke width (thicker for bold)
<x> <y> m             % stroke data: move
<x> <y> l             % stroke data: line
S                     % stroke
endstream
endobj
```

- BBox is in glyph units (not scaled to font size)
- No color operators inside -- color is inherited from graphics state
- No `q`/`Q` inside -- Form XObjects get implicit save/restore per PDF spec
- Stroke data uses glyph-local coordinates (origin at left baseline)

### Call Site Rendering

Normal glyph:
```
q
<color> RG
<sx> 0 0 <sy> <x> <y> cm
/HG42 Do
Q
```

Italic glyph (skew in matrix):
```
q
<color> RG
<sx> 0 <sx*0.2126> <sy> <x> <y> cm
/HG42 Do
Q
```

Where `sx = sy = fontSize / unitsPerEm`.

### Style Handling

- **Bold:** Baked into the XObject (thicker stroke width).  Separate GlyphFormKey.
- **Italic:** Skew transform (`tan(12deg) = 0.2126`) applied at the call site
  via the `cm` matrix.  Same XObject as normal weight.
- **Color:** Set at the call site before `Do`.  Not baked into the XObject.

### Resource Management

- Form XObjects are created lazily by `ensureGlyphForm()` during page rendering
- Each XObject is written to the PDF immediately when first needed
- Registered in the shared `resources.xObjects` dictionary
- All pages share the same resource dict, so all glyph forms are available
  to all pages
- Cache (`m_glyphForms`) is reset at the start of each `generate()` call

### Layer Architecture

| Layer | Content | Operators |
|-------|---------|-----------|
| Visible art | Glyph Form XObjects | `cm` + `Do` (no text ops) |
| Invisible text | Helvetica Base14 glyphs | `3 Tr` + `Tj` |
| ActualText | Markdown-decorated text | `BDC`/`EMC` with `/ActualText` |

No changes needed to the markdown copy layer.

### File Size Impact

Current inline paths: ~200 bytes per glyph occurrence (stroke operators).
XObject approach: ~200 bytes stored once per unique glyph, ~30 bytes per
invocation (`q cm /HGn Do Q`).  For a typical document with 50 unique glyphs
used 10,000 times: ~10KB + ~300KB vs ~2MB.  Roughly 6x smaller visible-layer
stream.

## Scope

**Phase 1 (this implementation):** Hershey fonts only.  Replace
`renderHersheyGlyphBox()` inline paths with Form XObject `Do` calls.

**Phase 2 (future):** TTF/OTF fonts.  Extract FreeType outlines into Form
XObjects using `m`/`l`/`c`/`f` operators.  Same `ensureGlyphForm` interface,
same call-site rendering.  CIDFont embedding becomes unnecessary for the
visible layer.

## Key Design Decision

Replace the current inline-stroke Hershey rendering entirely.  XObject
rendering is the only Hershey rendering path going forward (not a mode toggle).
