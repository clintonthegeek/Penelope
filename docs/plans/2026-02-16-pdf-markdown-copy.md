# PDF Markdown Copy Mode — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** When the user enables the "Embed markdown syntax" checkbox in the PDF export dialog, text copied from the exported PDF in any external viewer returns markdown syntax (`**bold**`, `# Heading`, `` `code` ``, `[link](url)`, etc.).

**Architecture:** Hidden glyphs rendered in the page background color (white) are drawn beneath visible text in the PDF content stream. Every PDF viewer extracts these as real characters during copy. The layout engine annotates glyph boxes with markdown prefix/suffix strings derived from the content model's inline node types. The PDF generator emits these as separate `BT`/`ET` blocks at tiny x-offsets to ensure correct reading order without visually disturbing the layout.

**Tech Stack:** Existing layout engine, PdfGenerator, FreeType glyph ID lookup. No new dependencies.

**Relationship to "Copy as Markdown" feature:** The existing Copy as Markdown feature (`DocumentView::copySelectionAsMarkdown()`) works at the viewer level — it maps selection rectangles to original markdown source lines via the source map. That feature is for copying within Penelope. This new feature embeds markdown syntax into the PDF itself so external viewers (Okular, Acrobat, etc.) return markdown. The two features are completely independent; no code is shared.

---

### Task 1: Add markdown decoration fields to GlyphBox

**Files:**
- Modify: `src/layout/layoutengine.h`

**Step 1: Add fields to GlyphBox**

After the `bool isListMarker` field (around line 58), add:

```cpp
    // Markdown copy mode: hidden prefix/suffix text for PDF markdown extraction
    QString mdPrefix;  // e.g. "**", "`", "[", "# "
    QString mdSuffix;  // e.g. "**", "`", "](url)"
```

**Step 2: Build**

Run: `make -C build -j$(($(nproc)-1))`

**Step 3: Commit**

```bash
git add src/layout/layoutengine.h
git commit -m "Add mdPrefix/mdSuffix fields to GlyphBox for markdown copy"
```

---

### Task 2: Add markdown decoration flag to layout engine

The layout engine should only compute markdown decorations when the export mode requests it, to avoid overhead for normal rendering.

**Files:**
- Modify: `src/layout/layoutengine.h`
- Modify: `src/layout/layoutengine.cpp`

**Step 1: Add setter to Engine class**

In `layoutengine.h`, in the `Engine` public section (after `setHyphenateJustifiedText`):

```cpp
    void setMarkdownDecorations(bool enabled) { m_markdownDecorations = enabled; }
```

Add to private members:

```cpp
    bool m_markdownDecorations = false;
```

**Step 2: Build**

Run: `make -C build -j$(($(nproc)-1))`

**Step 3: Commit**

```bash
git add src/layout/layoutengine.h
git commit -m "Add setMarkdownDecorations flag to layout engine"
```

---

### Task 3: Populate markdown decorations for inline nodes

The key insight: `collectInlines()` processes each `Content::InlineNode` and records the text range (start position + length) of each node in `CollectedText`. After `breakIntoLines()` creates glyph boxes (split by word boundaries), we can map each glyph box back to its originating inline node via `textStart` and annotate the first/last glyph box of each styled run with the appropriate markdown syntax.

**Files:**
- Modify: `src/layout/layoutengine.cpp`

**Step 1: Extend CollectedText with markdown ranges**

In the anonymous namespace, add a struct to track markdown decorations alongside the existing `CollectedText`:

```cpp
struct MarkdownRange {
    int textStart;       // position in collected text
    int textEnd;         // position + length
    QString prefix;      // markdown opening syntax
    QString suffix;      // markdown closing syntax
};
```

Add to `CollectedText`:

```cpp
    QList<MarkdownRange> markdownRanges;
```

**Step 2: Populate markdown ranges in collectInlines()**

Add a `bool markdownMode` parameter to `collectInlines()`:

```cpp
CollectedText collectInlines(const QList<Content::InlineNode> &inlines,
                              const Content::TextStyle &baseStyle,
                              bool markdownMode = false)
```

In each inline node handler, when `markdownMode` is true, compute and append a `MarkdownRange`:

For `Content::TextRun`:
```cpp
if (markdownMode) {
    QString prefix, suffix;
    bool bold = (n.style.fontWeight >= 700);
    bool italic = n.style.italic;
    bool strike = n.style.strikethrough;
    if (bold && italic) { prefix += QStringLiteral("***"); suffix.prepend(QStringLiteral("***")); }
    else if (bold)      { prefix += QStringLiteral("**");  suffix.prepend(QStringLiteral("**")); }
    else if (italic)    { prefix += QStringLiteral("*");   suffix.prepend(QStringLiteral("*")); }
    if (strike)         { prefix += QStringLiteral("~~");  suffix.prepend(QStringLiteral("~~")); }
    if (!prefix.isEmpty())
        result.markdownRanges.append({startPos, startPos + clean.size(), prefix, suffix});
}
```

For `Content::InlineCode`:
```cpp
if (markdownMode)
    result.markdownRanges.append({sr.start, sr.start + sr.length,
                                   QStringLiteral("`"), QStringLiteral("`")});
```

For `Content::Link`:
```cpp
if (markdownMode)
    result.markdownRanges.append({startPos, startPos + clean.size(),
                                   QStringLiteral("["),
                                   QStringLiteral("](") + n.href + QStringLiteral(")")});
```

**Step 3: Apply markdown ranges to glyph boxes after word splitting**

In `breakIntoLines()`, after the word-splitting loop (after the `words` list is built, around line 392), add:

```cpp
    // Apply markdown decorations to glyph boxes
    if (m_markdownDecorations && !collected.markdownRanges.isEmpty()) {
        for (const auto &range : collected.markdownRanges) {
            GlyphBox *first = nullptr;
            GlyphBox *last = nullptr;
            for (auto &w : words) {
                if (w.isNewline) continue;
                auto &gb = w.gbox;
                int gbEnd = gb.textStart + gb.textLength;
                // Check if this glyph box overlaps the markdown range
                if (gbEnd > range.textStart && gb.textStart < range.textEnd) {
                    if (!first) first = &gb;
                    last = &gb;
                }
            }
            if (first) first->mdPrefix += range.prefix;
            if (last) last->mdSuffix.prepend(range.suffix);
        }
    }
```

Note: `prepend` for suffix ensures correct nesting order (inner decorations close before outer ones).

**Step 4: Pass markdownMode flag through**

In `breakIntoLines()`, change the `collectInlines` call:

```cpp
    auto collected = collectInlines(inlines, baseStyle, m_markdownDecorations);
```

**Step 5: Build**

Run: `make -C build -j$(($(nproc)-1))`

**Step 6: Commit**

```bash
git add src/layout/layoutengine.cpp
git commit -m "Populate markdown prefix/suffix decorations on glyph boxes"
```

---

### Task 4: Populate heading-level markdown decorations

Headings need `# `, `## `, etc. prefix on the first glyph box of the first line.

**Files:**
- Modify: `src/layout/layoutengine.cpp`

**Step 1: In layoutHeading(), add heading prefix**

Find `Engine::layoutHeading()`. After the call to `breakIntoLines()` that produces the heading's lines, add:

```cpp
    if (m_markdownDecorations && !box.lines.isEmpty() && !box.lines.first().glyphs.isEmpty()) {
        QString headingPrefix = QString(heading.level, QLatin1Char('#')) + QLatin1Char(' ');
        box.lines.first().glyphs.first().mdPrefix.prepend(headingPrefix);
    }
```

**Step 2: Build**

Run: `make -C build -j$(($(nproc)-1))`

**Step 3: Commit**

```bash
git add src/layout/layoutengine.cpp
git commit -m "Add heading-level markdown prefix (# , ## , etc.)"
```

---

### Task 5: Add hidden glyph rendering to PdfGenerator

**Files:**
- Modify: `src/pdf/pdfgenerator.h`
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Add helper method declaration**

In `pdfgenerator.h`, add to the private section:

```cpp
    void renderHiddenText(const QString &text, FontFace *font, qreal fontSize,
                          qreal x, qreal y, QByteArray &stream);
```

**Step 2: Implement renderHiddenText()**

In `pdfgenerator.cpp`:

```cpp
void PdfGenerator::renderHiddenText(const QString &text, FontFace *font,
                                     qreal fontSize, qreal x, qreal y,
                                     QByteArray &stream)
{
    if (text.isEmpty() || !font)
        return;

    QByteArray fontName = pdfFontName(font);

    stream += "BT\n";
    stream += "/" + fontName + " " + pdfCoord(fontSize) + " Tf\n";
    stream += "1 1 1 rg\n";  // white fill — hidden beneath visible text

    qreal curX = x;
    for (int i = 0; i < text.size(); ++i) {
        uint cp = text[i].unicode();
        // Handle surrogate pairs
        if (QChar::isHighSurrogate(text[i].unicode()) && i + 1 < text.size()) {
            cp = QChar::surrogateToUcs4(text[i].unicode(), text[i + 1].unicode());
            ++i;
        }
        FT_UInt gid = FT_Get_Char_Index(font->ftFace, cp);
        if (gid == 0) continue;  // skip unmapped characters

        // Track glyph for font subsetting
        font->usedGlyphs.insert(gid);

        stream += "1 0 0 1 " + pdfCoord(curX) + " " + pdfCoord(y) + " Tm\n";
        stream += Pdf::toHexString16(static_cast<quint16>(gid)) + " Tj\n";

        // Advance by glyph width
        FT_Load_Glyph(font->ftFace, gid, FT_LOAD_NO_SCALE);
        qreal advance = static_cast<qreal>(font->ftFace->glyph->advance.x)
                         / font->ftFace->units_per_EM * fontSize;
        curX += advance;
    }

    stream += "ET\n";
}
```

**Step 3: Add FreeType include**

At the top of `pdfgenerator.cpp`, ensure `<ft2build.h>` and `FT_FREETYPE_H` are included. Check the existing includes — the font manager header may already pull these in. If not:

```cpp
#include <ft2build.h>
#include FT_FREETYPE_H
```

**Step 4: Build**

Run: `make -C build -j$(($(nproc)-1))`

**Step 5: Commit**

```bash
git add src/pdf/pdfgenerator.h src/pdf/pdfgenerator.cpp
git commit -m "Add renderHiddenText() for background-colored markdown glyphs"
```

---

### Task 6: Emit hidden markdown glyphs from renderLineBox()

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Add hidden text emission in renderLineBox()**

In `renderLineBox()`, find the loop that iterates over glyph boxes and calls `renderGlyphBox()`. The loop advances an `x` position variable. Before each `renderGlyphBox()` call, emit the prefix. After each call, emit the suffix.

Find the rendering section where glyph boxes are iterated (the block that computes `x` position and calls `renderGlyphBox`). Add markdown emission:

```cpp
    bool markdownMode = m_exportOptions.markdownCopy;
```

Before each `renderGlyphBox(gbox, stream, x, pdfY)` call:

```cpp
    if (markdownMode && !gbox.mdPrefix.isEmpty())
        renderHiddenText(gbox.mdPrefix, gbox.font, gbox.fontSize, x - 0.01, pdfY, stream);
```

After each `renderGlyphBox` call (but before advancing x past the glyph):

```cpp
    if (markdownMode && !gbox.mdSuffix.isEmpty())
        renderHiddenText(gbox.mdSuffix, gbox.font, gbox.fontSize, x + gbox.width + 0.01, pdfY, stream);
```

The tiny offsets (`-0.01` before, `+0.01` after) ensure PDF viewers sort the hidden text correctly in reading order.

**Step 2: Build**

Run: `make -C build -j$(($(nproc)-1))`

**Step 3: Commit**

```bash
git add src/pdf/pdfgenerator.cpp
git commit -m "Emit hidden markdown glyphs in renderLineBox when markdown mode active"
```

---

### Task 7: Wire markdown mode flag through the export pipeline

**Files:**
- Modify: `src/app/mainwindow.cpp`

**Step 1: Set markdown decorations on layout engine**

In `onFileExportPdf()`, after creating the `Layout::Engine layoutEngine(...)` (the final layout, not the pre-layout), add:

```cpp
    if (opts.markdownCopy)
        layoutEngine.setMarkdownDecorations(true);
```

This ensures the layout engine populates `mdPrefix`/`mdSuffix` only when the user enabled the "Embed markdown syntax" checkbox.

The pre-layout (used for page count) should NOT have markdown decorations since it's only for counting pages.

**Step 2: Build**

Run: `make -C build -j$(($(nproc)-1))`

**Step 3: Commit**

```bash
git add src/app/mainwindow.cpp
git commit -m "Wire markdown decoration flag through export pipeline"
```

---

## Files Changed Summary

| File | Change |
|------|--------|
| `src/layout/layoutengine.h` | Add `mdPrefix`/`mdSuffix` to `GlyphBox`, `setMarkdownDecorations()` to `Engine` |
| `src/layout/layoutengine.cpp` | `MarkdownRange` struct, populate in `collectInlines()`, apply to glyph boxes, heading prefix |
| `src/pdf/pdfgenerator.h` | Declare `renderHiddenText()` |
| `src/pdf/pdfgenerator.cpp` | Implement `renderHiddenText()`, emit from `renderLineBox()` |
| `src/app/mainwindow.cpp` | Set `setMarkdownDecorations(true)` when markdown mode selected |

## Verification

1. `make -C build -j$(($(nproc)-1))` — builds clean
2. Open a markdown file with bold, italic, inline code, links, and headings
3. Export PDF with "Markdown source" text copy mode
4. Open in Okular, select all text, copy
5. Paste into a text editor — should see `**bold**`, `*italic*`, `` `code` ``, `[text](url)`, `# Heading`
6. Export same file with "Embed markdown syntax" unchecked — copied text should be normal (no markdown syntax)
7. Verify visible rendering is identical between the two modes (hidden glyphs should be invisible)

## Not in Scope (Future Work)

- **Code block fences** (` ``` `): Code blocks use a different rendering path (`shapeAndBreak`). Can be added later.
- **List markers**: Visible bullets (`•`) would need hidden `- ` equivalents. Requires changes to `layoutList()`.
- **Block quotes**: Would need `> ` prefix on each line.
- **Unwrapped paragraphs mode**: Uses same hidden-glyph technique but embeds full paragraph text. Can be combined with markdown copy. Separate implementation.
