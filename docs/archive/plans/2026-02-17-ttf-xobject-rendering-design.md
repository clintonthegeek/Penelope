# TTF XObject Form Glyph Rendering (Phase 2)

## Problem

Phase 1 replaced Hershey stroke rendering with Form XObjects.  TTF/OTF fonts
still use CIDFont text operators (`BT`/`Tj`/`ET`) which participate in text
extraction, competing with the Layer 3 markdown copy invisible text.

## Solution

Extend the XObject glyph rendering to all fonts.  A per-export flag
`xobjectGlyphs` in `PdfExportOptions` enables rendering all glyphs
(TTF and Hershey) as Form XObjects.  No text operators in the visible layer.

## Architecture

### ensureGlyphForm extension

`GlyphFormKey` gains a `FontFace*` for TTF fonts:

```cpp
struct GlyphFormKey {
    const HersheyFont *hersheyFont = nullptr;
    FontFace *ttfFace = nullptr;
    uint glyphId = 0;
    bool bold = false;
};
```

`ensureGlyphForm` accepts both font sources:

```cpp
GlyphFormEntry ensureGlyphForm(const HersheyFont *hersheyFont,
                               FontFace *ttfFace,
                               uint glyphId, bool bold);
```

Two branches inside:
- **Hershey:** `m`/`l`/`S` stroked polylines (existing)
- **TTF:** `FT_Load_Glyph(FT_LOAD_NO_SCALE)` + `FT_Outline_Decompose` ->
  `m`/`l`/`c`/`f` filled outlines (new)

Both store coordinates in font units.  Scaling via `cm` at call site.

### TTF Form XObject structure

```
<objId> 0 obj
<< /Type /XObject /Subtype /Form
   /BBox [<xMin> <yMin> <xMax> <yMax>]
   /Length <len>
>>
stream
<x> <y> m
<x> <y> l
<cp1x> <cp1y> <cp2x> <cp2y> <x> <y> c
f
endstream
endobj
```

- No color operators inside (fill color inherited from graphics state)
- BBox from FreeType glyph metrics in font units
- Quadratic Beziers converted to cubic (same as existing outlineConicTo)

### Call site rendering

```
q
<color> rg                          % fill color (not stroke)
<sx> 0 0 <sy> <x> <y> cm           % scale + translate
/HG42 Do
Q
```

Where `sx = sy = fontSize / units_per_EM`.  TTF uses fill color (`rg`)
unlike Hershey which uses stroke color (`RG`).

### PdfExportOptions flag

```cpp
bool xobjectGlyphs = false;  // render all glyphs as Form XObjects
```

Added to `PdfExportOptions`, exposed in the PDF export dialog as a checkbox.

### Rendering dispatch

When `xobjectGlyphs` is true, all TTF glyph rendering routes through a
new `renderTtfGlyphBoxAsXObject` function instead of `renderGlyphBox` or
`renderGlyphBoxAsPath`.  Hershey rendering is unchanged (already uses
XObject Do from Phase 1).

### Font embedding

When `xobjectGlyphs` is true:
- CIDFont embedding is skipped entirely (no `embedFonts()`)
- If `markdownCopy` is also on, a Base14 Helvetica (`HvInv`) is registered
  for the invisible text layer (same pattern as Hershey mode)
- `m_hersheyMode` behavior is effectively generalized to all-XObject mode

### Layer architecture (unchanged)

| Layer | Content | Operators |
|-------|---------|-----------|
| Visible art | Glyph Form XObjects | `cm` + `Do` |
| Invisible text | Helvetica Base14 | `3 Tr` + `Tj` |
| ActualText | Markdown text | `BDC`/`EMC` |

### Trailing hyphen

The existing FreeType trailing-hyphen path (`FT_Outline_Decompose` inline)
is also converted to use `ensureGlyphForm` when `xobjectGlyphs` is true.
