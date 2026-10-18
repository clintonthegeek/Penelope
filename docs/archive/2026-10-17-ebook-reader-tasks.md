# Ebook-reader steps — 2026-10-17

This is the execution list for the [ebook-reader roadmap](2026-10-17-ebook-reader-roadmap.md). Do the steps in order. Each step is one change.

## Rules

- Do one step. Do not start the next step in the same change.
- Touch only the files named in the step. Do not reformat or rename unrelated code.
- Add the named test slot to `tests/regressiontest.cpp` in the existing `RegressionTest` class. Run it with:

```sh
cmake --build build --target penelope_tests
QT_QPA_PLATFORM=offscreen ./build/bin/penelope_tests <slotName>
```

Use the build directory you already have if it is not `build/`. A missing binary is a failed step. Do not skip the test.

- Do not assert wall-clock milliseconds. Assert counts, identities, cache flags, and equality with a full layout.
- Do not commit unless the user asked.
- Keep widgets and `PenelopeSettings` out of worker-only code. The layout engine already reads settings in a few places. Do not add new reads.
- Do not share FreeType or HarfBuzz objects between the composition worker and export.
- Cancellation is a callback that returns bool. Do not kill threads. Do not insert a cache entry after a cancel.

When a step says "the full layout", it means today's `Layout::Engine::layout()` or `layoutContinuous()` run to completion on the same input.

## EBOOK-01 — Cancel a long line break

### EBOOK-01a — The line breaker can return early

**Touch:** `src/layout/linebreaker.h`, `src/layout/linebreaker.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add `bool cancelled = false` to `LineBreaking::BreakResult`.
2. Add a last argument `const std::function<bool()> &cancelled = {}` to `findBreaks` and `findBreaksTiered`. Include `<functional>`.
3. In `findBreaks`, at the top of `for (int i = 0; i < n; ++i)` around the existing breakpoint loop, if `i % 32 == 0` and `cancelled` is non-empty and returns true, return a `BreakResult` with `cancelled == true` and an empty `breaks` list.
4. Pass the same callback from `findBreaksTiered` into `findBreaks`.
5. Leave every current caller compiling by using the default argument.

**Do not:** change demerit math, tolerances, or the active-node list.

**Test:** slot `ebook01FindBreaksObservesCancel`. Build a few thousand `Item::makeBox` / `makeGlue` items. Call `findBreaks` with a callback that returns false for the first two checks and true after that. Assert `result.cancelled` and `result.breaks.isEmpty()`. Call it again with an empty callback and assert `cancelled` is false and `breaks` is non-empty.

**Done when:** that slot passes, and `./build/bin/penelope_tests` still passes.

### EBOOK-01b — The layout engine aborts the document when a break is cancelled

**Touch:** `src/layout/layoutengine.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Where `breakIntoLines` calls `findBreaks` or `findBreaksTiered`, pass `[this] { return m_cancelled && m_cancelled(); }`.
2. If the returned `BreakResult::cancelled` is true, set a new private `bool m_breakCancelled` and return an empty `QList<LineBox>` from `breakIntoLines`.
3. Clear `m_breakCancelled` at the start of `layout()` and `layoutContinuous()`.
4. In both functions, the existing per-block cancel check stays. Immediately after each block is laid out, if `m_breakCancelled` is true, return `{}`. Do not append that block's elements.

**Do not:** return a document that contains the blocks laid out before the cancel. Callers already treat `{}` as "no result".

**Test:** slot `ebook01LayoutAbortsOneParagraph`. Build a `Content::Document` with one paragraph whose text is the sentence `The quick brown fox jumps. ` repeated 4,000 times. Set the engine's cancellation check to return true. `layout()` returns zero pages. `layoutContinuous()` returns zero elements. A second call with the check cleared returns at least one line. Use the test file's existing font and shaper setup if one exists. If it does not, construct a `FontManager` and `TextShaper` the same way `tests/renderbench.cpp` does inside `measureOnce`.

**Done when:** `ebook01LayoutAbortsOneParagraph` passes.

### EBOOK-01c — A cancelled shape is not cached

**Touch:** `src/font/textshaper.h`, `src/font/textshaper.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add `void setCancellationCheck(std::function<bool()> check)` next to `setCacheLimit`. Store it beside the cache. An empty function means never cancelled.
2. In `shapeUncached`, at the start of `for (const InternalRun &run : textRuns)`, if the check is set and returns true, return the runs already built only when the list is still empty. If any `ShapedRun` was already appended, return `{}`.
3. In `shape()`, you already increment `m_cacheMisses` and call `shapeUncached` on a miss. Insert into `m_cache` only when the check is empty or returns false after `shapeUncached` returns, and the returned list is non-empty. A cancel must leave `cacheCost()` unchanged for that key.
4. `setCancellationCheck({})` clears it.

**Do not:** change the cache key. Do not clear the cache when the check fires.

**Test:** slot `ebook01ShapeDoesNotCachePartial`. Use two `StyleRun`s on one string so itemization produces work. Set the check to return true. Call `shape`. Assert the result is empty. Clear the check. Call `shape` on the same input. Assert `cacheMisses` increased again, so the cancelled call was not stored. Call `shape` once more. Assert `cacheHits` increased.

**Done when:** `ebook01ShapeDoesNotCachePartial` passes.

### EBOOK-01d — The worker sets both checks from the request token

**Touch:** `src/render/compositioncontroller.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. In `CompositionWorker::render`, the engine already gets `setCancellationCheck`. Call `m_shaper->setCancellationCheck` with the same token test before layout, and `setCancellationCheck({})` before the function returns, including on the cancel and exception paths.
2. Do not change `submit`, `cancel`, or the one-pending queue.

**Do not:** emit a result for a cancelled id. `finish()` already drops ids that are not `m_latestId`.

**Test:** slot `ebook01SupersededHugeParagraphDeliversShort`. Submit a continuous request whose body is one paragraph of 4,000 repetitions of `The quick brown fox jumps. `. Immediately submit a second continuous request whose markdown is `# Ok\n\nShort.\n`. Wait on `ready` with `QTRY_COMPARE_WITH_TIMEOUT(..., 10000)`. Assert one ready result, its markdown is the short document, and its continuous layout has elements. Assert `failed` was not emitted for the short id.

**Done when:** `ebook01SupersededHugeParagraphDeliversShort` and `asynchronousCompositionCoalescesAndReusesContent` pass.

## EBOOK-02 — Slice a tall block, and skip lines while painting

### EBOOK-02a — Style lookup is a binary search

`resolveStyleAt` in `src/layout/layoutengine.cpp` walks every style run from the start. It is a file-local function.

**Touch:** `src/layout/layoutengine.cpp`, `src/layout/stylerunindex.h` (new), `tests/regressiontest.cpp`.

**Do:**

1. Add `stylerunindex.h` with one function:

```cpp
int styleRunIndexAt(int charPos, const QList<StyleRun> &runs);
```

`StyleRun` is the layout collector's run, not `TextShaper`'s. If that type is private to the cpp file, move the struct into this header. `styleRunIndexAt` returns the index of the run where `start <= charPos < start + length`, or `-1`. The runs are sorted by `start`. Use a binary search. Do not scan from index 0.

2. Replace the loop in `resolveStyleAt` with a call to `styleRunIndexAt`.

**Do not:** change which style fields are copied into the result.

**Test:** slot `ebook02StyleRunIndexMatchesEdges`. Build runs `[0,10)`, `[10,5)`, `[20,1)`. Assert indices for positions 0, 9, 10, 14, 20, and `-1` for 19 and 21.

**Done when:** `ebook02StyleRunIndexMatchesEdges` passes.

### EBOOK-02b — A code fence is broken one source line at a time

**Touch:** `src/layout/layoutengine.cpp` (`layoutCodeBlock`), `tests/regressiontest.cpp`.

**Do:**

1. Before editing, add slot `ebook02CodeLineCount` for this fence and make it pass against the current engine:

````
```cpp
int a = 1;
int b = 2;

int c = 3;
```
````

Record the line count it lays out. The later edit must keep that count.

2. In `layoutCodeBlock`, split `cb.code` on `\n`, keeping empty lines. For each source line, build that line's inline list the same way the function builds inlines today (highlight, then the plain fallback), and call `breakIntoLines` for that line only. Append those `LineBox`es onto `box.lines`.
3. Between source lines, if `m_cancelled` is set and returns true, set `m_breakCancelled` and return an empty `BlockBox`. The document loops from EBOOK-01b then return `{}`.
4. Keep one `BlockBox` for the fence. Do not emit one element per source line. Background, padding, and `codeLanguage` stay on that box.

**Do not:** change paragraph or heading breaking in this step. Do not change `assignToPages`.

**Test:** `ebook02CodeLineCount` still passes. Add `ebook02CancelBetweenCodeLines`: a 2,000-line fenced `cpp` block, cancellation check returns true, `layoutContinuous()` has zero elements.

**Done when:** both slots pass and a highlighted fence still produces colored lines. Assert one line in the 5-line fixture carries a foreground other than the base style if the syntax theme marks `int`.

### EBOOK-02c — Painting skips lines outside the clip

**Touch:** `src/render/boxtreerenderer.h`, `src/render/boxtreerenderer.cpp`, `src/canvas/webviewitem.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add `void setLineClip(const QRectF &clip)` on `BoxTreeRenderer`. A null `QRectF` means paint every line. Store it.
2. Add `int lastPaintedLineCount() const`. `renderBlockBox` resets the counter when it starts lines, and increments it only when it calls `renderLineBox`.
3. In the line loop, skip a line when the clip is non-null and the line's vertical span (`box.y + lineY` through that plus `line.height`) misses `clip`. Still advance `lineY`.
4. `WebViewItem::paint` calls `m_renderer.setLineClip(exposed)` before the element loop and `setLineClip(QRectF())` after it.
5. PDF drawing must not set a clip. Leave `PdfGenerator` unchanged so it paints whole blocks.

**Do not:** remove the element binary search in `WebViewItem::firstVisibleElement`.

**Test:** slot `ebook02ClipSkipsLines`. Lay out an 80-line fenced block continuously. Paint the block through `QtBoxRenderer` with a clip of height 30 at the top of the block. Assert `lastPaintedLineCount()` is greater than 0 and less than the line count. Paint again with a null clip. Assert the count equals the line count.

**Done when:** `ebook02ClipSkipsLines` and the existing viewport-culling regression pass.

## EBOOK-03 — Identities for blocks

### EBOOK-03a — Every block gets an id at build time

**Touch:** `src/model/contentmodel.h`, `src/model/contentbuilder.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add `quint64 blockId = 0` to `Paragraph`, `Heading`, `CodeBlock`, `BlockQuote`, `List`, `Table`, `HorizontalRule`, and `FootnoteSection`.
2. Add `quint64 m_nextBlockId = 1` on `ContentBuilder`. Reset it to 1 in `build()`.
3. Whenever `build()` creates one of those blocks, assign `m_nextBlockId++`. Nested list items and quote children get their own ids. Do not reuse an id inside one `build()` call.

**Do not:** persist ids to disk. Do not put ids in Markdown or RTF output.

**Test:** slot `ebook03BuildAssignsUniqueIds`. Build a document with a heading, a paragraph, a list with a nested list, a block quote, a table, a fence, and a footnote. Walk every block, including children. Assert each `blockId` is non-zero and unique. Run the existing plain-text and RTF tests. They must pass without asserting on ids.

**Done when:** `ebook03BuildAssignsUniqueIds` passes.

### EBOOK-03b — Reuse ids when the surrounding blocks did not change

**Touch:** `src/model/blockidentity.h`, `src/model/blockidentity.cpp`, `src/CMakeLists.txt` if the cpp is not picked up by a glob, `tests/regressiontest.cpp`.

**Do:**

1. Add `void restabilizeBlockIds(const QList<Content::BlockNode> &previous, QList<Content::BlockNode> &current)`.
2. Fingerprint a block as its type tag plus its visible plain text. Use `Content::toPlainText` on a one-block document, or a private walker if that is awkward. Do not include `blockId`, colors, or font names in the fingerprint.
3. Walk the two lists from the start. When the fingerprints are equal, copy `blockId` from previous onto current, including equal children in order.
4. When they differ, search forward at most 4 blocks on each side for a fingerprint that occurs once in that window on both lists. Copy that id. If a fingerprint occurs twice in the window, assign no old id. Leave the fresh id from EBOOK-03a.
5. After the first unmatched tail, leave the fresh ids. Do not search the rest of the document.

**Do not:** call this from the layout engine.

**Test:** slot `ebook03RestabilizeKeepsNeighbors`. Build two documents from source. The second inserts `Inserted.\n\n` after the first paragraph. Run `restabilizeBlockIds`. The heading before the insert and the paragraph after it keep their ids. The inserted paragraph keeps a new id. Add a second case: two identical paragraphs in a row. Their ids must not swap relative to a one-character edit in the first of them. Assert the edited paragraph has a new id and the untouched copy keeps its id.

**Done when:** `ebook03RestabilizeKeepsNeighbors` passes.

### EBOOK-03c — The worker restabilizes after a parse

**Touch:** `src/render/compositioncontroller.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. The worker already keeps the last semantic document. After a parse that did not reuse that document, call `restabilizeBlockIds` with the previous blocks and the new blocks before storing the new document.
2. The first document in the process has no previous. Skip the call.

**Do not:** restabilize when `reusedContent` is true. Those blocks are already the cached ones.

**Test:** slot `ebook03WorkerKeepsIdsAcrossInsert`. Submit `# Title\n\nAlpha.\n\nBeta.\n`. On `ready`, record the ids for Alpha and Beta. Submit the same text with `Inserted.\n\n` between them. Assert Alpha and Beta ids match and Inserted is different. Use `QTRY_COMPARE_WITH_TIMEOUT` at 10 seconds.

**Done when:** `ebook03WorkerKeepsIdsAcrossInsert` passes.

## EBOOK-04 — Cache fragments on the worker

### EBOOK-04a — A bounded cache object with two maps

**Touch:** `src/layout/fragmentcache.h`, `src/layout/fragmentcache.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add `FragmentCache`. It holds two `QCache`s, each with `setMaxCost` default `4 * 1024 * 1024`.
2. Shaping key: `quint64 blockId`, `quint64 contentHash`, font family, pixel size as `int` of `size * 100`, weight, italic. No width. No color.
3. Break key: the shaping key plus width in `qRound(width * 100)` and `bool hyphenate`.
4. Stored break value: `QList<Layout::LineBox>` plus the glyph ids to replay. Stored shaping value can be the same line boxes for this step. Paragraphs only. Provide `bool storeParagraph(...)`, `bool loadParagraphBreaks(...)`, `bool loadParagraphShapes(...)`.
5. `storeParagraph` returns false and stores nothing when the estimated cost is greater than `maxCost`. It must not evict other entries in that case. `QCache::insert` already refuses an oversized cost. Check the return value.
6. `loadParagraphShapes` on a hit is independent of width. `loadParagraphBreaks` misses when the width key differs.

**Do not:** call the cache from `Engine` yet.

**Test:** slot `ebook04FragmentCacheKeys`. Store a one-line paragraph. Load breaks at the same width hits. Load breaks at another width misses. Load shapes at another width hits. A store whose cost is larger than the max returns false and leaves `totalCost()` unchanged.

**Done when:** `ebook04FragmentCacheKeys` passes.

### EBOOK-04b — The engine consults the cache for paragraphs and headings

**Touch:** `src/layout/layoutengine.h`, `src/layout/layoutengine.cpp`, `src/render/compositioncontroller.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add `void setFragmentCache(FragmentCache *cache)` on `Engine`. Null is the default and means no cache.
2. In `layoutParagraph` and `layoutHeading`, after the block's plain text and style key are known, try `loadParagraphBreaks`. On a hit, copy the line boxes onto the block, set positions the same way the uncached path does, replay glyph usage through `FontManager::markGlyphUsed` for each stored glyph, and skip `breakIntoLines`.
3. On a break miss and a shape hit, still call `breakIntoLines`. Shaping reuse inside `TextShaper` already covers that call when the text matches. Do not build a second shaper.
4. On a full miss, break as today, then `storeParagraph`.
5. The worker constructs one `FragmentCache` member on `CompositionWorker` and passes it to the engine for every render. Do not put the cache on `CompositionRequest`.

**Do not:** cache tables, lists, quotes, images, or footnotes. They take the existing path.

**Test:** slot `ebook04ParagraphCacheRoundTrip`. Lay out three paragraphs into a continuous result with a cache. Lay out again at the same width. The second result's line counts and source anchors equal the first. Change the width by 40. Line breaks may differ. The plain text of each paragraph stays the same. A third layout with a null cache matches the first layout's line counts at the original width.

**Done when:** `ebook04ParagraphCacheRoundTrip` passes and `ebook01SupersededHugeParagraphDeliversShort` still passes.

### EBOOK-04c — Code lines use the same cache

**Touch:** `src/layout/layoutengine.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. After EBOOK-02b, each source line of a fence is one `breakIntoLines` call. Give that line a cache key of the fence `blockId`, the zero-based line index, and the line's text hash.
2. Store and load those lines through two new methods on `FragmentCache`: `storeCodeLine` and `loadCodeLine`. Same oversized-entry rule.
3. A hit skips `breakIntoLines` for that line and still appends the stored `LineBox`es.

**Do not:** cache the whole fence as one entry.

**Test:** slot `ebook04CodeLineCacheHits`. Lay out a 30-line fence twice at the same width with one cache. Expose a counter `FragmentCache::breakHits` and `breakMisses`, or return enough from the engine for the test to see hits. The second layout's break-load count for code lines equals 30. A one-character edit of line 3 misses that line and hits the other 29. Add the counters if they do not exist. Do not add a log.

**Done when:** `ebook04CodeLineCacheHits` passes.

## EBOOK-05 — A cursor that can stop and resume

### EBOOK-05a — Define the cursor and run it to the end

**Touch:** `src/layout/layoutengine.h`, `src/layout/layoutengine.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add this type next to `Engine`:

```cpp
struct LayoutCursor {
    int nextBlock = 0;
    bool finished = false;
};
struct LayoutBudget {
    int maxBlocks = 0;     // 0 means no block cap
    qreal maxHeight = 0;   // 0 means no height cap; continuous mode
    bool onePage = false;  // print mode: stop after filling one page
};
```

2. Add `ContinuousLayoutResult layoutContinuousFrom(const Content::Document &doc, qreal width, LayoutCursor *cursor, const LayoutBudget &budget)`.
3. It lays out blocks starting at `cursor->nextBlock`. It stops before a block that would pass `maxBlocks` or `maxHeight`, leaving that block index in `nextBlock`. It sets `finished` when `nextBlock` reaches `doc.blocks.size()`.
4. Rewrite `layoutContinuous()` as a loop that calls `layoutContinuousFrom` with an empty budget until `finished`, concatenating elements and source-map entries. Y positions stay monotonic, which they already are if each call continues `y` from the cursor. Store `qreal y` on the cursor and pass it back.

**Do not:** change `layout()` in this step. Do not deliver partial results to `MainWindow`.

**Test:** slot `ebook05ContinuousCursorMatchesFull`. For a document of 12 paragraphs, a cursor with `maxBlocks = 5` followed by a resume matches one `layoutContinuous()` call in element count, text, and source line numbers. A budget of 0 matches in one call.

**Done when:** `ebook05ContinuousCursorMatchesFull` passes.

### EBOOK-05b — Print layout fills one page and resumes

**Touch:** `src/layout/layoutengine.h`, `src/layout/layoutengine.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add the page-assignment inputs the cursor needs: current page number, y on the page, and whether the previous block had keep-with-next. Put them on `LayoutCursor`.
2. Add `bool layoutNextPage(const Content::Document &doc, const PageLayout &pageLayout, LayoutCursor *cursor, Page *outPage)`.
3. It consumes blocks from `cursor->nextBlock`, using the existing `assignToPages` rules for widows, keep-with-next, and table headers. Extract the per-element decision from `assignToPages` into a helper the loop can call. Do not copy the rules into a second algorithm.
4. `layout()` becomes a loop over `layoutNextPage` until `cursor.finished`.

**Do not:** change PDF generation.

**Test:** slot `ebook05PagedCursorMatchesFull`. A document that already splits a table or a code block in the regression suite, plus 20 paragraphs, paginates through the cursor and through `layout()`. Page count, per-page element counts, and source-map line numbers match.

**Done when:** `ebook05PagedCursorMatchesFull` passes.

### EBOOK-05c — The worker can fill one budget and stop

**Touch:** `src/render/compositioncontroller.h`, `src/render/compositioncontroller.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add to `CompositionRequest`: `int maxBlocks = 0`, `qreal maxHeight = 0`, `bool onePage = false`. Zero and false mean the full document, which is today's behavior and must stay the default.
2. Add to `CompositionResult`: `bool finished = true`, `LayoutCursor cursor`.
3. `CompositionWorker::render` uses `layoutContinuousFrom` or `layoutNextPage` when a budget is set, and the old full path when it is not.
4. A budgeted render emits one `completed` and returns. It does not loop inside the worker. The controller stays one-job, one-pending.

**Do not:** change `MainWindow::installComposition`. Full-document opens stay full-document until EBOOK-06.

**Test:** slot `ebook05WorkerHonorsBlockBudget`. Submit a continuous request of 30 paragraphs with `maxBlocks = 4`. The ready result has `finished == false`, more than 0 elements, and fewer blocks than 30. Submit the same source with `maxBlocks = 0`. The result is `finished` and has every paragraph.

**Done when:** `ebook05WorkerHonorsBlockBudget` passes.

## EBOOK-06 — Web view shows a prefix

### EBOOK-06a — Append to the web item

**Touch:** `src/canvas/webviewitem.h`, `src/canvas/webviewitem.cpp`, `src/canvas/documentview.h`, `src/canvas/documentview.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add `WebViewItem::appendElements(QList<Layout::PageElement> elements, qreal newHeight)`. It appends, updates `totalHeight`, calls `prepareGeometryChange()`, and `update()` only for the new vertical range. It does not clear `m_linksByElement`.
2. Add `DocumentView::appendWebContent(Layout::ContinuousLayoutResult &&extra)`. Require `m_webViewItem` already non-null and `m_pdfMode` false. Call `appendElements`. Grow `sceneRect`. Do not call `m_scene->clear()`. Do not change the scroll bar value.
3. `setWebContent` stays the path for a new document or a new width. It may still clear.

**Do not:** start a composition from `DocumentView`.

**Test:** slot `ebook06AppendDoesNotResetScroll`. This can be a widget test under `QT_QPA_PLATFORM=offscreen`. Create a `DocumentView`, install a 20-paragraph continuous result, set the vertical scroll bar to a non-zero value, append 10 more paragraphs, and assert the scroll value is unchanged and the scene height grew.

**Done when:** `ebook06AppendDoesNotResetScroll` passes.

### EBOOK-06b — The first web request asks for one screen

**Touch:** `src/app/mainwindow.cpp` (`compositionRequest`, `installComposition`, `rebuildCurrentDocument`), `tests/regressiontest.cpp`.

**Do:**

1. When `request.continuous` is true, set `request.maxHeight` to the viewport height in scene points plus one extra viewport of runway. Use the same width formula `compositionRequest` already uses. Convert with `zoomPercent`.
2. `installComposition`, for a continuous result with `finished == false`, calls `setWebContent` if the view has no web item or the width changed, and `appendWebContent` if this result's `cursor.nextBlock` continues the cursor already installed. Store the installed cursor on `DocumentTab` or on `DocumentView` as `LayoutCursor m_layoutCursor`.
3. After installing an unfinished prefix, if this tab is still current, submit a follow-up request with the same source revision and width, `maxHeight` equal to one more viewport, and a new field `int startBlock` equal to `cursor.nextBlock`. Add `startBlock` to `CompositionRequest` and honor it in `layoutContinuousFrom`.
4. A follow-up must not cancel the in-flight job if the in-flight job is that follow-up. Only a new source revision, a new width, or a closed tab cancels. Practically: if the only difference is `startBlock` moving forward, do not call `submit` in a way that cancels the job you just started. Submit the follow-up from `installComposition` after the previous job has finished, which it has, because `ready` already ran.
5. The width check `qAbs(width - request.width) > 0.01` stays for a new width. A scrollbar appearing is not a new width for this check. Before the first layout, subtract `verticalScrollBar()->sizeHint().width()` from the available width when estimating `maxHeight` suggests the document will scroll. Use the hint even if the bar is not visible yet.

**Do not:** change print requests. Their `maxBlocks` stays 0 in this step.

**Test:** slot `ebook06PrefixDoesNotLayoutEveryBlock`. Build the request the worker sees for a 200-paragraph document and a viewport height of 800. Drive `CompositionController` directly with that budget rather than `MainWindow` if constructing `MainWindow` is too heavy. Assert the first result is unfinished and its element count is less than 200. Resume from `cursor.nextBlock` until finished. The concatenated element count equals one full `layoutContinuous` of the same document.

**Done when:** `ebook06PrefixDoesNotLayoutEveryBlock` passes.

### EBOOK-06c — Scroll and idle resume the cursor

**Touch:** `src/canvas/documentview.cpp`, `src/app/mainwindow.cpp`.

**Do:**

1. Add signal `DocumentView::moreLayoutRequested()`.
2. Emit it from `scrollContentsBy` when web mode is active, the layout cursor is not finished, and the distance from the scroll value to the maximum is less than one viewport.
3. Also emit it from a single-shot 0 ms timer after the first prefix installs, so idle time fills the next screen without waiting for a scroll.
4. `MainWindow` connects it to the same follow-up submit as EBOOK-06b. Ignore the signal when a composition is busy or a follow-up is already pending. `CompositionController::isBusy()` and `pendingCount()` are enough.

**Do not:** emit the signal in print mode.

**Test:** slot `ebook06NearEndRequestsMore`. A `DocumentView` with an unfinished cursor, scrollbar forced near the bottom, `scrollContentsBy(0, 1)` emits `moreLayoutRequested` once. A second scroll while a `bool` guard the test sets does not emit again if you also gate on a `m_moreRequested` flag inside the view. Add that flag, cleared by a new `DocumentView::layoutExtended()` which `appendWebContent` calls.

**Done when:** `ebook06NearEndRequestsMore` passes.

### EBOOK-06d — A width change rebreaks the visible range first

**Touch:** `src/app/mainwindow.cpp`, `src/render/compositioncontroller.cpp`.

**Do:**

1. On a web width change, the new request uses the fragment cache from EBOOK-04. It does not need a special path for that. Set `maxHeight` to the visible range plus one runway, `startBlock` to the first block whose old y is at or above the current scroll, if those y values are known. Otherwise `startBlock` is 0.
2. Installing that result uses `setWebContent`, not append, because y positions changed. Restore the scroll fraction from `saveViewState()` after install. `installComposition` already captures view state. Keep that.
3. Then continue with follow-up requests for the tail.

**Do not:** delete the fragment cache on a width change. Shaping keys have no width. Break keys do.

**Test:** slot `ebook06WidthChangeReusesShapes`. Lay out 40 varied paragraphs at width 400 through the worker with a cache. Submit width 460 and `maxBlocks` covering all of them. Assert `shapeHits > 0` and `reusedContent` is true. Assert `finished` after the full budget. This reuses `CompositionResult::shapeHits`.

**Done when:** `ebook06WidthChangeReusesShapes` passes.

## EBOOK-07 — Print view shows page 1 first

### EBOOK-07a — Debounce page-setup edits

**Touch:** `src/widgets/pagelayoutwidget.cpp`, `src/app/mainwindow.cpp`.

**Do:**

1. In `PageLayoutWidget`, stop connecting margin spin boxes' `valueChanged` directly to `pageLayoutChanged`. Connect them to a single-shot `QTimer` of 150 ms that emits `pageLayoutChanged` once. Restart the timer on each value change.
2. Orientation and page-size combos may keep emitting immediately.
3. `MainWindow::onPageLayoutChanged` stays as it is.

**Do not:** debounce theme changes or source edits in this step.

**Test:** slot `ebook07MarginEditsCoalesce`. Construct the widget offscreen. Spy on `pageLayoutChanged`. Set the top margin three times, 10 ms apart. Assert the spy is still empty. Wait 200 ms with `QTest::qWait`. Assert the spy count is 1.

**Done when:** `ebook07MarginEditsCoalesce` passes.

### EBOOK-07b — Deliver the first page, then the rest

**Touch:** `src/render/compositioncontroller.h`, `src/render/compositioncontroller.cpp`, `src/canvas/documentview.h`, `src/canvas/documentview.cpp`, `src/app/mainwindow.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. A print request (`continuous == false`) from `compositionRequest` sets `onePage = true` only for the interactive reader. Find the export and print-dialog call sites (`MainWindow` PDF export and `PrintController` setup). Those callers build their own generator path and must keep using a full `Engine::layout` or a cursor with no budget. Do not set `onePage` on them.
2. Add `DocumentView::setPdfPrefix(const QByteArray &pdf, bool finished, int pageCount)`. The first call does what `setPdfData` does. A later call for the same document appends page items for new page indexes only. Do not `m_scene->clear()` on an append. Do not reset the scroll bar.
3. Generating a PDF for a prefix: the worker runs `layoutNextPage` once, generates a PDF of the pages so far or of the new page only. Prefer generating PDF bytes for the new page and appending items. If the current `PdfGenerator` cannot emit one page into an existing document, generate a fresh PDF of all pages laid out so far and replace Poppler, but still do not clear scroll. Name which of the two you did in a comment on `setPdfPrefix`.
4. `installComposition` on an unfinished print result installs the prefix and submits `onePage` again with `startBlock = cursor.nextBlock`. Same rule as EBOOK-06b: submit after `ready`, do not cancel the job that just finished.
5. The status bar page count uses the laid-out count until `finished`, then the exact count. Find the existing page-count label and feed it `pageCount`.

**Do not:** remove Poppler. Do not make window resize call `rebuildCurrentDocument`. `DocumentView::resizeEvent` must stay web-only.

**Test:** slot `ebook07FirstPageBeforeLast`. A 40-paragraph print request with `onePage` true returns one page and `finished == false`. Resuming until finished yields the same page count as a full `layout()`.

**Done when:** `ebook07FirstPageBeforeLast` passes.

### EBOOK-07c — Zoom does not recompose

**Touch:** `src/canvas/documentview.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Confirm `setZoomPercent` in print mode only updates `PdfPageItem` zoom. It already does. Add a regression so it stays true.
2. `resizeEvent` must not emit `webRelayoutRequested` when `m_renderMode != WebMode`.

**Test:** slot `ebook07ZoomDoesNotRelayout`. Spy on `webRelayoutRequested`. Call `setZoomPercent` in print mode. Assert the spy is empty. Send a `QResizeEvent` with a new width in print mode. Assert the spy is empty. Switch to web mode, send a width change, assert the spy fired.

**Done when:** `ebook07ZoomDoesNotRelayout` passes.

## EBOOK-08 — Paint print pages from boxes

### EBOOK-08a — A page item that paints one page's boxes

**Touch:** `src/canvas/boxpageitem.h`, `src/canvas/boxpageitem.cpp`, `src/CMakeLists.txt`, `tests/regressiontest.cpp`.

**Do:**

1. Add `BoxPageItem : public QGraphicsItem`. It holds `std::shared_ptr<const Layout::Page>`, the page size, and a `std::shared_ptr<FontManager>`.
2. `paint` fills the page background and calls `QtBoxRenderer::renderElement` for elements on that page only. Set no line clip.
3. `boundingRect` matches `PdfPageItem`: the page size plus the 4 pt shadow.

**Do not:** construct a `Poppler::Document`. Do not read `PenelopeSettings`.

**Test:** slot `ebook08BoxPagePaintsAfterFontsReleased`. Lay out one short page. Paint the item into a `QImage` through a `QPainter`. The image's non-white pixel count is greater than 0. Destroy the `Engine` before painting. The `FontManager` is kept alive by the `shared_ptr` on the item. Painting still succeeds.

**Done when:** `ebook08BoxPagePaintsAfterFontsReleased` passes.

### EBOOK-08b — Header and footer come from one plan

**Touch:** `src/print/headerfooterplan.h` (new), `src/pdf/pdfgenerator.cpp`, `src/print/headerfooterrenderer.cpp`, `src/canvas/boxpageitem.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add a value type `HeaderFooterPlan` with the strings and rectangles the PDF writer already computes for one page: left, center, right, for header and footer, and the page rectangle.
2. Extract the existing substitution of `{page}`, `{pages}`, filename, and title into a function `HeaderFooterPlan makeHeaderFooterPlan(...)` that both the PDF writer and `BoxPageItem` call. Move the substitution out of the PDF writer. Do not reimplement the tokens.
3. `BoxPageItem::paint` draws that plan with `QPainter` after the body. Use the same font size the PDF path uses. Read it from the plan, not from settings.
4. `{pages}` uses the count passed in. EBOOK-07's running count is the caller-supplied value.

**Do not:** support per-master body margins. They are not implemented.

**Test:** slot `ebook08HeaderPlanTokens`. A plan for page 2 of a running count of 9 and filename `a.md` contains `2` and `9` in the footer string that uses `{page} / {pages}`, and does not contain the raw token `{page}`.

**Done when:** `ebook08HeaderPlanTokens` passes and an exported one-page PDF still contains the footer text. Use the existing PDF text extraction in the regression tests if it is there. If it is not, extract with Poppler in the test.

### EBOOK-08c — Interactive print installs box pages and does not build a PDF

**Touch:** `src/render/compositioncontroller.cpp`, `src/app/mainwindow.cpp`, `src/canvas/documentview.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add `CompositionRequest::bool interactivePages = false`. `compositionRequest` sets it true for the on-screen print reader. Export and the print dialog leave it false.
2. When `interactivePages` is true, the worker fills `result->paginated` via the cursor and leaves `result->pdf` empty. It still fills `finished` and `cursor`.
3. `installComposition` in that mode calls a new `DocumentView::setBoxPages(shared page list, fonts, finished)` instead of `setPdfData`. The first call clears the scene and adds a `BoxPageItem` per page. A continuation appends items for new page indexes only.
4. `setRenderMode(PrintMode)` still applies.

**Do not:** delete `setPdfData`. Export still uses it indirectly through Poppler only if some other feature needs it. The on-screen path must not call `PdfGenerator`.

**Test:** slot `ebook08InteractivePrintHasNoPdf`. Submit a print request with `interactivePages` true and `onePage` true. Assert `pdf.isEmpty()`, `paginated.pages.size() == 1`, and `finished` is false.

**Done when:** `ebook08InteractivePrintHasNoPdf` passes and the export function in `MainWindow` still calls `PdfGenerator`. Confirm by reading the export function. Do not remove that call.

## EBOOK-09 — Hit testing on the boxes

### EBOOK-09a — Build a per-page index from line boxes

**Touch:** `src/layout/hitindex.h`, `src/layout/hitindex.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Add `struct HitIndex` with `QVector<QRectF> lineBounds`, the source start and end offsets those lines already carry, and `QVector<QRectF> linkBounds` plus the href.
2. Add `HitIndex buildHitIndex(const Layout::Page &page)`. Walk elements on that page only. Use each line's position and the source range already stored on the line or its glyph boxes. Do not map glyph ids back to Unicode.
3. Add `int hitLine(const HitIndex &index, QPointF pagePos)`.

**Do not:** change selection code yet.

**Test:** slot `ebook09HitIndexFindsLine`. Lay out a two-paragraph page. A point inside the second paragraph's first line returns that line. A point in the margin returns `-1`.

**Done when:** `ebook09HitIndexFindsLine` passes.

### EBOOK-09b — Box pages use the index for links and selection

**Touch:** `src/canvas/boxpageitem.cpp`, `src/canvas/documentview.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. `BoxPageItem` builds the `HitIndex` once in the constructor.
2. Add `QString BoxPageItem::linkAt(QPointF itemPos)` and `QRectF BoxPageItem::lineRect(int index)`.
3. In `DocumentView`, the print-mode link and selection paths that call Poppler must branch: when the page item is a `BoxPageItem`, use the hit index. When it is a `PdfPageItem`, keep Poppler. Find those call sites by searching `DocumentView` for `Poppler` and `linkAt`.
4. Copy, including Markdown and RTF, already uses source ranges. Feed it the source range from the hit index. Do not write a second copy implementation.

**Do not:** remove the Poppler branch. Export preview is gone, but tests and any leftover PDF item still need it.

**Test:** slot `ebook09BoxLinkAndSelection`. A one-page document with `[go](https://example.com)` in the body. `linkAt` on that word's rect returns the URL. A selection of that word returns a source range whose text contains `go` and does not contain `https://example.com`.

**Done when:** `ebook09BoxLinkAndSelection` passes.

### EBOOK-09c — Find resumes the cursor until the match is laid out

**Touch:** `src/canvas/documentview.cpp` or the search helper it calls, `src/app/mainwindow.cpp`.

**Do:**

1. Find the existing find-next function. Search the semantic document's plain text first.
2. If the match's block index is greater than or equal to the installed cursor's `nextBlock`, submit follow-up page or web budgets until the cursor passes that block, then scroll. Do this by setting a `int m_scrollToBlock` on the tab and handling it in `installComposition`.
3. If the match is already laid out, scroll as today.

**Do not:** call `layout()` on the whole document from the GUI thread.

**Test:** slot `ebook09FindResumesCursor`. A worker result that stopped before block 10. The match is in block 12. After the test feeds two more budgeted results that advance the cursor past 12, the stored `m_scrollToBlock` is cleared and the view's scroll target is block 12's y. Drive the objects directly. Do not open a window.

**Done when:** `ebook09FindResumesCursor` passes.

## EBOOK-10 — Reuse pages after an edit

### EBOOK-10a — Fingerprint a page by what flowed into it

**Touch:** `src/layout/layoutengine.h`, `src/layout/pagefingerprint.cpp` (new), `tests/regressiontest.cpp`.

**Do:**

1. Add `quint64 pageFingerprint(const LayoutCursor &incoming, const QList<quint64> &blockIds, quint64 blockContentHashes, bool keepWithNextIn, int tableHeaderRows, int noteCount)`. Include those inputs. Hash them with `qHashMulti` or a stable combination. Do not hash pointers.
2. Store the fingerprint on `Layout::Page` as `quint64 fingerprint`.
3. `layoutNextPage` sets it from the cursor state at the start of the page plus the ids it placed.

**Do not:** skip layout based on the fingerprint yet.

**Test:** slot `ebook10FingerprintStable`. Two cursors with the same ids, hashes, and incoming y produce the same fingerprint. Changing incoming y changes it. Changing a block id changes it.

**Done when:** `ebook10FingerprintStable` passes.

### EBOOK-10b — Reuse a matching tail, and refuse a bad fingerprint

**Touch:** `src/layout/layoutengine.cpp`, `src/render/compositioncontroller.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. The worker may keep the last `LayoutResult` for the current source's print layout. On a new print layout at the same page size, lay out pages until the first page whose incoming fingerprint differs from the stored page at that index.
2. From that page downward, lay out fresh. Above it, copy the stored pages.
3. If a stored page's fingerprint equals the new incoming fingerprint but its block ids differ, ignore the stored page and lay out fresh. This is the bad-fingerprint case. Implement it as: reuse only when fingerprint, block ids, and content hashes all match.
4. A table, a footnote, or a header that contains `{page}` makes the page ineligible for reuse. Detect a table or footnote element, or a header plan that used `{page}` / `{pages}`, and lay out from that page down.

**Do not:** reuse pages across a page-size or margin change. The caller passes `bool geometryChanged`.

**Test:** slot `ebook10ReuseTailAfterInsert`. A 15-paragraph document paginates fully. Insert a paragraph on page 0. The rebuilt result's page count may grow by one. A page that started after the insertion point and whose incoming fingerprint matches is copied: assert the same block ids. A second test flips one stored fingerprint before reuse. Assert that page's block ids were laid out again. You can assert that by changing the inserted text's hash and expecting the following page's y values to be recomputed. Comparing the new full `layout()` page count and the last page's source lines is enough.

**Done when:** `ebook10ReuseTailAfterInsert` passes and `ebook05PagedCursorMatchesFull` still passes.

## After the last step

Run the full suite:

```sh
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Run the view benchmark only in a `RelWithDebInfo` build, and only to record numbers. Do not fail a step on a millisecond:

```sh
QT_QPA_PLATFORM=offscreen QT_HASH_SEED=0 ./build/bin/penelope_renderbench \
  --views --sizes 400,1600 --shape all --iterations 1
```

Report block counts, `finished` flags, shape hits, and whether a code-block stripe paint is smaller than the unculled paint. Those are the roadmap's checks.
