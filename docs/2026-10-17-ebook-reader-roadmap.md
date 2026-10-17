# Ebook-reader roadmap — 2026-10-17

Penelope should open and resize a long Markdown file the way an ebook reader does. Typeset the text on screen, keep the result, and keep going. A PDF reader never typesets because the page is already a display list. Penelope still has to typeset, because line breaks depend on width. It should stop typesetting the whole book before it draws the first screen, and stop throwing that work away on every resize.

This is the implementation plan. The [long-document benchmark](2026-10-17-long-document-benchmark.md) is the evidence. The [October task list](2026-10-04-performance-tasks.md) is superseded; each old identifier absorbed here is named on the task.

Execute [the step list](2026-10-17-ebook-reader-tasks.md). Those steps are binding. Do one step, then stop. Do not re-decompose a step into a different design.

## Target behavior

- Opening a long file parses it, then lays out the first screen and paints it. Later blocks follow on idle time and as the user scrolls.
- The previous frame stays up while new text is typeset. A resize rebreaks the visible range first. Finished text above that range stays.
- Web scrolling paints the lines that intersect the viewport, including inside a tall code block or paragraph.
- Print view shows page 1 when page 1 exists. Zoom and window resize rasterize pages already produced. They do not paginate again. Changing page size or margins is a typesetting change: debounce it, then rebuild from the affected page.
- The page count may be incomplete until the background pass finishes. It then becomes exact.
- Export, the print dialog, and RTF still run the layout cursor to the end and generate a full document. That path is allowed to be slow.

On the recorded run, a 1.5 MB prose file spent about 140 ms parsing, 620 ms laying out, and 670 ms writing a preview PDF. A 3,200-line highlighted fence spent 2.3 s inside one line-breaking call, and a resize could not enter that call. Those are the costs this plan removes from the interactive path. Parse of the whole source stays for now. At that size it is the smaller floor.

## What stays

Keep the worker boundary in [CompositionController](../src/render/compositioncontroller.h). Requests stay value copies. The worker owns FreeType and HarfBuzz. GUI painters touch immutable font bytes and Hershey data. One job runs at a time, with one pending replacement. Export does not borrow the reader's mutable engine or its font context.

Keep the semantic document, the box tree, `QtBoxRenderer`, and the shaping cache. Whole-document `Engine::layout()` and `Engine::layoutContinuous()` remain the definition of a correct full result. The cursor-driven path has to agree with them. PDF generation stays the export and print-dialog implementation.

## Target pipeline

```
source revision
    |
    v
parse once  ->  Content::Document, block identities
    |
    v
layout cursor (worker)
    |  budget: one screen, or one page
    |  cache: shaped runs, then line breaks at this width
    v
display list for the finished range
    |
    +-- web:  paint visible lines
    +-- print: paint or raster finished pages
    |
    v
idle / scroll / later page  ->  resume the same cursor
```

A width, margin, or style change invalidates line breaks. It reuses the parse and, when the text is unchanged, the shaped runs. A source edit invalidates identities from the edit downward. A theme change that alters metrics invalidates breaks. A color-only change invalidates paint and must not look like a geometry hit.

## Rules for every task

Read the named files and the regression tests that already cover them. Use a `RelWithDebInfo` build for any speed comparison. Record samples with `penelope_renderbench --views`. Do not assert wall-clock thresholds in CTest. Do assert structure: how many blocks were laid out, whether a cache hit, whether cancellation returned, whether a partial result matches the same prefix of a full result.

Preserve working-tree changes. Run the Qt regression suite. Do not combine two tasks into one rewrite of `DocumentView`. Keep widgets and `PenelopeSettings` out of worker code. Do not share a mutable FreeType or HarfBuzz instance between export and the reader. Cooperative cancellation only: check between units of work, never kill the thread, never cache a partial shape.

## Phase 1 — Make one block interruptible and reusable

The cursor is useless while one fence is an atomic multi-second call.

### EBOOK-01 — Cancel inside line breaking and shaping

Absorbs PERF-02.

**Scope:** `src/layout/layoutengine.cpp` (`breakIntoLines`), `src/layout/linebreaker.cpp`, `src/font/textshaper.cpp`. The engine already has `setCancellationCheck`.

Check the callback between itemization runs and between line-break candidates. A cancelled `shape()` must not insert a partial cache entry. Callers that pass no callback keep today's behavior.

**Acceptance:** a document whose body is one huge paragraph can be cancelled while a GUI timer keeps firing. No stale result is installed. The following short request completes. Cache entries produced before the cancel still replay complete glyph runs. Report the delay from cancel to return separately from throughput.

### EBOOK-02 — Break tall blocks into lines the painter can skip

**Scope:** `layoutCodeBlock()` and the paragraph path in `src/layout/layoutengine.cpp`, `src/canvas/webviewitem.cpp`, `src/render/boxtreerenderer.cpp`. Depends on EBOOK-01.

A highlighted fence is one `breakIntoLines()` over every span, and `resolveStyleAt()` scans the span list from the start for every run. The painter then draws every line in the element. Split code into line units before breaking, or index the style runs so the scan is not quadratic, and clip paint to lines that intersect `exposedRect`. A plain paragraph taller than the viewport gets the same paint clip. Page splitting of a code block must still agree with today's PDF fragments.

**Acceptance:** `--views --shape code --sizes 3200` width-pass layout moves toward the wall fixture's slope (1,377 plain lines laid out in about 28 ms on the recorded machine; the code fixture took 2.3 s). A 120 px stripe paint no longer tracks the full block height. Cancelling mid-fence returns in EBOOK-01's bound. Cached and uncached full pagination of a fenced block still match. Element-level culling of ordinary prose stays in place.

## Phase 2 — Remember blocks, and resume layout

### EBOOK-03 — Stable block identities

Absorbs INCR-01.

**Scope:** `src/model/contentmodel.h`, `src/model/contentbuilder.cpp`, a small identity helper. No page-assignment changes.

Source line numbers are positions, not identities. Assign a stable identity to each top-level block and carry it through nested list, quote, and table structure. Match unchanged neighbors across an insertion or deletion. Identical repeated paragraphs must not steal each other's identity. Style changes keep the identity and change the metric key. When matching is ambiguous, drop the tail and rebuild it.

**Acceptance:** inserting a paragraph preserves identities around it. Editing one block invalidates that block only. A style-map change keeps identities. RTF and Markdown copy are unchanged.

### EBOOK-04 — Cache shaped runs and line breaks separately

Absorbs INCR-02.

**Scope:** layout engine and a bounded cache owned by the composition worker. Depends on EBOOK-03. Uses the font context the worker already owns.

Key shaped runs by block identity, content revision, and the style inputs `TextShaper` already keys. Key line breaks by that plus available width and hyphenation settings. Positions and page numbers stay outside the cached fragment. Color is a paint input, not a geometry hit. The cache is bounded. A hit replays glyph usage so a later PDF subset stays valid. Paragraphs, headings, and code lines from EBOOK-02 go in first. Every other block type has an explicit bypass that lays out without storing.

**Acceptance:** editing one paragraph reuses the others. A width change misses line breaks and hits shaping. A color-only change cannot reuse stale paint data. After an insertion, source anchors below the edit move. A full layout built from the cache matches a full layout that skips it, including PDF text extraction for a cached paragraph. A block larger than the entry limit is laid out and not stored, rather than evicting the whole cache for one miss.

### EBOOK-05 — Layout cursor

**Scope:** `src/layout/layoutengine.h`, `src/render/compositioncontroller.cpp`. Depends on EBOOK-04.

Add a cursor that, given a semantic document and a budget, lays out the next screen's worth of blocks or the next page and returns a continuation. `layout()` and `layoutContinuous()` become loops over that cursor so export keeps one definition of a full document. The cursor checks cancellation between blocks and between the line units from EBOOK-02. Page assignment for print still applies widows, keep-with-next, and table-header repetition, starting from the cursor's incoming page state.

**Acceptance:** running the cursor to the end matches today's paginated and continuous results on the existing regression documents, including a split table and a split code block. Stopping after the first budget and resuming matches doing the work in one call. A cancel leaves a continuation that the next request may discard. No GUI type appears in the cursor.

## Phase 3 — Show the first screen

### EBOOK-06 — Web view paints a prefix and extends it

**Scope:** `CompositionController` result delivery, `MainWindow::installComposition`, `DocumentView::setWebContent`, `WebViewItem`. Depends on EBOOK-05.

The worker may deliver more than one result per open: a prefix, then extensions of that same source revision and width. Installing an extension appends laid-out blocks. It does not clear the scene or jump the scroll position. The first delivery is the viewport plus a short runway. Idle time and scrolling near the end of the laid-out range resume the cursor. The table of contents can list headings from the semantic document immediately; scroll targets fill in as their blocks are positioned.

A width change rebreaks the visible range first, using EBOOK-04. Text above the viewport from the previous width may stay until the new prefix is ready. The 0.01 pt width check in `installComposition` must not discard a prefix because the scrollbar appeared. Apply the scrollbar width to the continuation, or reserve the scrollbar width when the content can exceed the viewport.

**Acceptance:** a 6,400-paragraph fixture paints a first screen without laying out every paragraph. Extending does not reset scroll. A width change reports shaping hits for unchanged prose. The scrollbar appearing does not schedule a second whole-document layout. Stripe paint of a tall block stays on the EBOOK-02 side of the benchmark. Source revision and mode checks still drop stale deliveries.

### EBOOK-07 — Print view publishes pages as they exist

**Scope:** worker pagination, `DocumentView::setPdfData` / page items, `MainWindow` page-count display. Depends on EBOOK-05. Does not yet remove Poppler.

Deliver pages in batches. Page 1 is installed when it exists. Further batches append page items. The reported count is the number laid out, and becomes exact when the cursor finishes. Headers that say `{page} / {pages}` may show the running total until then; they must update when the total is known, without retypesetting body text.

Window resize and zoom do not call `rebuildCurrentDocument()`. They only change which existing pages rasterize. Margin, orientation, and page-size edits are debounced. `PageLayoutWidget` currently emits `pageLayoutChanged` on every `QDoubleSpinBox::valueChanged`, and `onPageLayoutChanged` rebuilds immediately. Coalesce those into one typesetting job.

**Acceptance:** the first page of the 6,400-paragraph fixture is delivered before the last page is laid out. A window resize during that pass does not restart pagination. Zoom re-rasters and does not recompose. Dragging a margin spinbox submits one job after the control settles, not one job per step. Export of the same source still contains every page.

## Phase 4 — Stop using a PDF as the print-view picture

### EBOOK-08 — Paint finished pages from their boxes

Absorbs PREVIEW-01, PREVIEW-02, and PREVIEW-05.

**Scope:** a const page snapshot, a page item that uses `QtBoxRenderer`, header/footer drawing shared with `PdfGenerator`, the worker's print output. Depends on EBOOK-07.

Interactive print view paints the display list it already built. Ordinary reading does not serialize a PDF and does not ask Poppler for page geometry. Export and the print dialog still call `PdfGenerator` on a cursor that has run to completion. Header and footer fields come from a value plan both backends consume, including the running page count from EBOOK-07.

**Acceptance:** no PDF bytes are produced for an ordinary print-view open. Page geometry matches an exported PDF at portrait and landscape. Text, rules, tables, and transparent images paint. The snapshot remains paintable after the controller is destroyed. A parity corpus still compares an exported PDF against the painted pages. If a case fails, name it and keep export on the PDF path. Do not add a user-facing switch that offers both readers.

### EBOOK-09 — Selection, search, and links on the display list

Absorbs PREVIEW-03 and PREVIEW-04.

**Scope:** a per-page hit index built from shaped boxes, and the `DocumentView` adapters that today use Poppler. Depends on EBOOK-08.

Index links, code-block regions, source anchors, and line bounds from the boxes. Selection, word selection, find next/previous, heading scroll, and Markdown, plain, and RTF copy read that index. Search may scan the semantic document before the target page exists, then resume the cursor until the match is laid out and scroll to it.

**Acceptance:** the interaction tests that pass on the Poppler reader pass on painted pages, including a split code block, a nested list, a ligature, and a repeated table header. Copying a selection returns the same Markdown it does today. Find in an unlaid-out region does not typeset the whole file before showing the match.

## Phase 5 — Edits after the first screen

### EBOOK-10 — Resume pagination after an edit

Absorbs INCR-03.

**Scope:** page-state fingerprint beside the cursor. Depends on EBOOK-03, EBOOK-04, and EBOOK-07.

An edit invalidates from the earliest affected block, including a preceding keep-with-next. Reuse a following page only when its incoming state and fragment fingerprint match. Tables, notes, and page-number-dependent headers are part of that state. Where the fingerprint cannot be defined yet, fall back to laying out the tail. A wrong fingerprint must redo work rather than show a stale page.

**Acceptance:** incremental and full results agree for an insertion at a page boundary, a widow, a heading, and a page-number shift. Tests show a downstream page reused unchanged, and a deliberately bad fingerprint refusing that reuse.

## Work that is real and not this project

Do not start these in place of the tasks above. They remain useful after the cursor exists.

| Old id | Why it waits |
| --- | --- |
| PERF-01 | `--views` already records cold, width, paint, and raster. Extend it inside EBOOK-06 and EBOOK-07 instead of adding a second mode. |
| PERF-03 | Reusing the ICU break iterator is a local saving. Measure it after tall blocks are sliced. |
| PERF-04 | Image documents bypass the semantic cache. Worth doing, and it does not make the first screen lazy. |
| PERF-05 | Retune the shaping-cache limit only after EBOOK-02 and EBOOK-04. A larger limit does not store one giant fence. |
| PERF-06 | Sharing `QRawFont` across web items is a few milliseconds on the recorded prose paints. |
| PERF-07 | Suppressing palette notifications avoids needless rebuilds. Orthogonal to the cursor. |

## Non-goals

- A streaming Markdown parser. The first screen may wait for the parse. Hyphenating lazily per block can follow if that floor shows up in `--views` parse time.
- Replacing MD4C, HarfBuzz, or the PDF writer.
- Reflowing print view on window resize. That is the PDF-reader behavior to keep.
- Deleting the full-document layout functions. Export uses them via the cursor.
- A user setting that chooses Poppler preview against box preview.

## Suggested order

The binding sequence is [EBOOK-01a through EBOOK-10b](2026-10-17-ebook-reader-tasks.md). Do not run EBOOK-06 beside EBOOK-05. Each later step assumes the earlier test slots exist and pass. The summaries above describe the goal of a group. The step file names the edit.

The benchmark document's prose, code, and wall fixtures are the manual check after EBOOK-10b, in a `RelWithDebInfo` build. They are not a CTest gate.
