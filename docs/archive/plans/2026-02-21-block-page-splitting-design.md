# Block Page Splitting Design

## Problem

When any block (code block, paragraph, blockquote, list item) is taller than the
remaining page space, the layout engine pushes it to the next page. If it exceeds
a full page height, it overflows off the bottom. Only tables have splitting logic
(`splitTable()` with header repetition).

## Solution

Add general block splitting to `assignToPages()` so that any `BlockBox` can break
at line boundaries across pages. Each block type's visual properties (background,
border, padding, blockquote border) carry over to each fragment. A separate split
function handles `FootnoteSectionBox`.

## Design Decisions

- **Orphan/widow minimum**: 2 lines per fragment. A block with fewer than 4 lines
  that doesn't fit is pushed whole to the next page rather than split 1/3 or 2/2
  awkwardly.
- **Fragment styling**: Each fragment is an independent styled box. Code block
  fragments each get their own background rectangle and border — no open-ended
  styling at break points.
- **Approach**: A single `splitBlockBox()` function covers all BlockBox types.
  FootnoteSectionBox gets a separate `splitFootnoteSection()`.

## splitBlockBox()

```
splitBlockBox(block, availableHeight, minLines=2)
  -> std::optional<std::pair<BlockBox, BlockBox>>
```

Algorithm:
1. If `block.lines.size() < minLines * 2`, return nullopt (can't satisfy
   orphan+widow with a split).
2. Walk lines accumulating height. Find the last line where cumulative height
   plus padding fits within `availableHeight`.
3. If fewer than `minLines` fit, return nullopt (orphan).
4. If fewer than `minLines` remain, return nullopt (widow).
5. Create two BlockBox copies:
   - Fragment 1: lines [0..splitIdx], spaceAfter = 0
   - Fragment 2: lines [splitIdx+1..end], spaceBefore = 0
6. Both fragments inherit all visual fields: type, background, border, padding,
   blockQuoteLevel, hasBlockQuoteBorder, codeLanguage, codeFenced, source, width, x.
7. Heights recomputed from the lines in each fragment plus padding.

## assignToPages() Integration

Current logic:
- Block doesn't fit, page not empty -> push to next page
- Block doesn't fit, page IS empty -> place anyway (overflow)

New logic:
- Block doesn't fit, page not empty:
  - Try splitBlockBox(block, remainingSpace)
  - If split succeeds: place fragment1, push fragment2 back into element queue
  - Else: push whole block to next page
- Block doesn't fit, page IS empty:
  - Try splitBlockBox(block, pageHeight)
  - If split succeeds: place fragment1, push fragment2 into queue
  - Else: place anyway (overflow fallback for blocks with < 4 lines)

Fragment2 re-enters the same loop, so a 3-page code block naturally produces
3 fragments without explicit recursion.

## CodeBlockRegion

Each code block fragment emits its own CodeBlockRegion with the same source line
range. The hit-testing in `codeBlockIndexAtScenePos()` matches by source lines,
so multiple regions mapping to the same Content::CodeBlock works correctly.
Language override uses `cb->code.trimmed()` as key (content-based), unaffected
by splitting.

## FootnoteSectionBox Splitting

```
splitFootnoteSection(box, availableHeight)
  -> std::optional<std::pair<FootnoteSectionBox, FootnoteSectionBox>>
```

Splits between footnotes (not within a single footnote):
- Fragment 1: footnotes [0..splitIdx], separator line
- Fragment 2: footnotes [splitIdx+1..end], no separator
- If zero footnotes fit, return nullopt (push whole section)

## Markdown Copy Mode

Two new fields on BlockBox to distinguish fragment position:

```cpp
bool isFragmentStart = true;   // first or only fragment
bool isFragmentEnd = true;     // last or only fragment
```

Unsplit blocks: true/true. splitBlockBox sets fragment1 to true/false, fragment2
to false/true. If fragment2 splits again, first half becomes false/false.

Rendering behavior:
- Code block opening fence (```language\n): only if isFragmentStart
- Code block closing fence (```\n\n): only if isFragmentEnd
- Block separator (\n\n or \n): only if isFragmentEnd
- Middle fragments: no fences, no separators

## Files Modified

- `src/layout/layoutengine.h` — add isFragmentStart/isFragmentEnd to BlockBox,
  declare splitBlockBox() and splitFootnoteSection()
- `src/layout/layoutengine.cpp` — implement split functions, update assignToPages()
- `src/pdf/pdfgenerator.cpp` — update renderBlockBox() to use fragment flags for
  markdown fence emission
