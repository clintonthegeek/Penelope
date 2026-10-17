# Performance implementation tasks — 2026-10-04

Superseded by the [ebook-reader roadmap](2026-10-17-ebook-reader-roadmap.md). Implement from that document. The tasks below are the notes it absorbs (PERF-02, INCR-01..03, PREVIEW-01..05) or explicitly defers (PERF-01, PERF-03..07). Do not start a PREVIEW or INCR branch from this file.

The hard scheduling and font-ownership boundary is implemented in [CompositionController](../src/render/compositioncontroller.h); see [implementation notes](2026-10-04-composition-implementation.md). These tasks were designed to be assigned individually. Complete the listed acceptance checks and report limitations with each change. Do not combine the direct-preview tasks into an unsolicited rewrite of DocumentView.

For each task, read its named files and relevant existing regression cases. Use an optimized build for performance measurements, preserve working-tree changes, and run the Qt regression suite. Do not spawn/delegate or commit unless instructed. Keep GUI widgets/settings out of worker code. Never share mutable FreeType/HarfBuzz instances between export and composition threads.

## Immediately assignable improvements

### PERF-01 — Expose useful composition measurements in the benchmark

**Scope:** tests/renderbench.cpp, tests/CMakeLists.txt, new benchmark documentation. No rendering changes.

Add a `--composition` mode using CompositionController directly. Submit one cold request, then the identical request, then a width-only request. Record worker parse/layout/PDF times, semantic reuse, shaping hits/misses, total request-to-ready time and source size in JSON. Use the existing synthetic document generator. Preserve existing CLI modes. Separate pipeline timing from first paint and raster completion; print a timeout as a failure rather than a timing of zero.

**Acceptance:** all three requests produce valid content; the warm/width requests reuse semantics; changed width retains shaping hits; no same-thread composition is introduced. A smoke input completes in CTest. Keep large size curves optional. Record individual samples and build type rather than assert machine-dependent timing thresholds.

### PERF-02 — Cancellation inside long shaping/line-breaking work

**Scope:** font/textshaper.*, layout/linebreaker.*, layout/layoutengine.*. Depends on the current cancellation API; no scheduler redesign.

Pass a cooperative cancellation callback into the line breaker and TextShaper. Check at safe boundaries between itemization runs and line-break candidates. Do not interrupt library calls or use thread termination. Cancelled calls must not populate a cache with partial results. Retain the default behavior when no callback is supplied.

**Acceptance:** cancel a document with one unusually large paragraph while a GUI heartbeat timer runs; no stale result/error is delivered and the next short request finishes. Verify caches still emit complete valid glyph runs after cancellation. Measure cancellation delay separately from throughput.

### PERF-03 — Reuse ICU line-break iterators within an engine

**Scope:** layout/layoutengine.*. No global iterator or shared cross-thread state.

Replace per-paragraph iterator construction in `breakIntoLines()` with an engine-owned iterator. Capture the locale once for the engine, reset text each use, and keep current failure fallback. Account for recursive layout/table measurement so reusing the iterator does not invalidate data still in use.

**Acceptance:** existing pagination/source-map tests pass. Add multilingual break comparisons and a construction-count check demonstrating reuse across paragraphs. Benchmark CPU time on prose and tables; do not claim a gain if noise encompasses the effect.

### PERF-04 — Track image dependencies for safe semantic reuse

**Scope:** model/contentbuilder.*, render/compositioncontroller.*. No filesystem watcher overhaul.

Have ContentBuilder record each resolved local image path and its observed fingerprint, including missing files. Store dependencies with the semantic cache. Reuse a cached image-bearing document only if every dependency is unchanged. Use a content digest or a documented metadata-plus-digest policy; an image overwritten with the same size/mtime must not silently remain cached. Remove the source-string `![` bypass only after dependency tests pass.

**Acceptance:** unchanged image source reuses semantics; replacement updates geometry/pixels; deletion shows the current placeholder/alt behavior; creating a formerly missing asset triggers reparse; relative paths in different base directories remain distinct. Disabled-image requests preserve alt text. No file reads occur in the GUI result-install callback.

### PERF-05 — Improve shaping-cache observability and sizing

**Scope:** font/textshaper.*, render/compositioncontroller.*, benchmark mode from PERF-01. No font-metric changes.

Expose entry count, eviction count and estimated retained bytes. Measure cache behavior at 8/16/32 MiB across repeated edits and width changes using representative documents. Select a bounded default based on results, not an unlimited map. Avoid a preference UI unless requested.

**Acceptance:** bounded cost, exact eviction counters, disabled-cache behavior, cache hits after width changes, glyph-usage replay and font-feature key separation all pass. Report both throughput and memory tradeoffs, including a document larger than the cache working set.

### PERF-06 — Preserve GUI raw-font cache across continuous layout replacements

**Scope:** canvas/qtboxrenderer.*, canvas/webviewitem.*, canvas/documentview.*. Depends on current shared font ownership.

Move the QRawFont cache to a GUI-owned object that compatible WebViewItems can share. Keys must use a stable font-context identity plus face identity and size; do not key only on an address that can be reused after destruction. The cache must retain the necessary immutable font data/owner and have a size limit. Create QRawFonts on the painting thread. Reset/evict safely when the font context changes.

**Acceptance:** repeated same-font layouts do not reconstruct QRawFont for every item; a new font context cannot reuse an old face's font; old contexts are released after eviction/items disappear; controller-destruction painting test still passes. No worker invokes GUI cache methods.

### PERF-07 — Suppress known presentation notifications at their source

**Scope:** widgets/documenttab.*, app/mainwindow.*, tests/regressiontest.cpp. No parser or worker changes.

Add a DocumentTab helper for GUI-owned palette/highlighter-color updates. Suppress source-editor signals only while applying those known presentation operations. Retain the existing text comparison for notifications outside this helper; do not assume QTextDocument revision changes mean characters changed. Preserve real edit, undo/redo, file-reload and modified-state notifications. Use the helper in MainWindow instead of separately applying palette and highlighter colors.

**Acceptance:** theme changes leave source text, source revision and dirty state unchanged and schedule no source rebuild. Genuine typing/undo/redo still invalidates composition exactly once. File reload still emits its explicit change. Instrument the source-read path in a focused test to show known presentation changes no longer convert the full editor to plain text. Avoid blocking all document signals or suppressing asynchronous user edits.

## Direct paginated preview: implement as gated stages

This is a larger feature. Each task below should produce an independently reviewable component. Keep the existing Poppler reader as the default until the integration gate passes.

### PREVIEW-01 — RenderSnapshot contract and page-item prototype

**Scope:** new render snapshot type and a new canvas page item, plus tests. No MainWindow behavior switch. Depends on shared ownership already implemented.

Define a const snapshot that owns a paginated LayoutResult, semantic/source-map data, immutable font resources and PageLayout. Implement a page QGraphicsItem using QtBoxRenderer to paint one page's body with the correct content origin and background. Include landscape geometry. Keep the snapshot alive in the item; do not borrow worker-local layout storage. Constructor/setup must run on GUI thread.

**Acceptance:** page dimensions and body origins match exported PDF at both orientations; render text, transparent images, tables and rules; items paint after controller destruction. No code changes selection/search behavior yet. Bound painting by the requested page; do not traverse all document pages for one paint.

### PREVIEW-02 — Shared header/footer drawing plan

**Scope:** pdf/pdfgenerator.*, print/headerfooterrenderer.*, a new value-only header/footer plan helper, tests. Depends on PREVIEW-01.

Extract resolved header/footer fields and their positioned layout from PDF generation into a backend-neutral drawing plan. Inputs include filename/title, page number/count, active master page and page geometry. Both PDF and QPainter must consume the same plan. Preserve current odd/even/first master selection and all implemented decorations. Freeze date/time inputs per snapshot where needed.

**Acceptance:** PDF output semantics remain equivalent; first/odd/even, landscape and multiline fields paint consistently in the prototype; page counts/filenames do not come from stale GUI state. Do not claim to implement different body margins for each master in this task.

### PREVIEW-03 — Source/glyph-based hit-test index

**Scope:** new layout hit-test helper and tests. No UI mode switch. Depends on PREVIEW-01.

Build a value-only per-page index from shaped boxes: links, code blocks, source anchors, line/glyph bounds and text ranges. Include nested lists/quotes, repeated table headers, footnotes, soft hyphens, ligatures, combining marks and RTL. Define explicitly which source cluster corresponds to each selectable visual range. Reuse shaping clusters; do not reconstruct text by glyph ID.

**Acceptance:** coordinates select the intended source ranges on each page; links and language overrides work in nested structures; ligatures/marks do not split invalid source offsets; repeated fragments do not duplicate copied source. Document unsupported cases. Keep the helper usable by both readers.

### PREVIEW-04 — Search and selection adapters for the prototype

**Scope:** canvas/documentview.* and helpers from PREVIEW-03. Depends on PREVIEW-02/03.

Add an internal direct-preview adapter for selection painting, copy MIME formats, search navigation and link activation. Preserve source Markdown and styled RTF copy. Keep the Poppler adapter intact. Add a developer/test-only constructor option or internal switch; do not add a confusing public setting while the adapter is incomplete.

**Acceptance:** cross-page and reverse selection, word selection, find next/previous, scroll-to-heading, nested code-language menus, Markdown/plain/RTF copy, all page arrangements and zoom pass shared interaction tests in both adapters.

### PREVIEW-05 — Enable direct preview only after parity checks

**Scope:** render/compositioncontroller.*, app/mainwindow.*, canvas/documentview.* and integration tests. Depends on all previous PREVIEW tasks.

Give the worker request a direct-preview output option that skips preview PDF generation but retains snapshot data. Install the page items. Export/print must still generate their full PDFs independently. Preserve an exact-PDF/Poppler path internally for parity tests and fallback. Make the default switch only after shared behavior tests pass.

**Acceptance:** no PDF serialization on ordinary direct-preview rebuilds; latest-only scheduler and ownership tests continue passing; interactions from PREVIEW-04 pass. Compare raster/geometry against exported PDFs over a committed multilingual corpus. Measure UI responsiveness and peak memory, not just serialization time. If parity is incomplete, keep the prototype disabled and identify the missing test cases.

## Incremental layout groundwork

### INCR-01 — Stable block identities and revision-safe source anchors

**Scope:** content model/building and a matching helper. No incremental page assignment yet.

Introduce stable top-level block identities across edits using deterministic matching of unchanged neighboring blocks. Source line numbers are positions, not identities. Include nested structure fingerprints without tying identity to transient glyph pointers or font IDs. Define ambiguous repeated-paragraph matching conservatively; full rebuild is an acceptable fallback.

**Acceptance:** inserting a paragraph preserves identities of unchanged blocks; editing text invalidates the changed block; repeated identical paragraphs cannot cause stale content reuse; style changes retain identities but invalidate metric keys. Serialization/RTF behavior is unchanged.

### INCR-02 — Width-dependent block-layout cache

**Scope:** layout/layoutengine.*, bounded cache helper and tests. Depends on INCR-01 and current font ownership.

Cache unpositioned layout fragments using block identity/content revision, resolved metrics, width, hyphenation/table settings and immutable font context. Keep page positions/source offsets outside reusable metric payloads. The cache must be bounded and replay glyph usage for PDF subsetting. Start with paragraphs/headings; bypass tables and notes initially.

**Acceptance:** one-paragraph edits reuse unaffected metrics; width changes invalidate line breaks; color-only changes cannot reuse stale paint styles; source anchors shift correctly after insertion; cached and uncached full pagination agree. Give an explicit bypass for every unsupported block type.

### INCR-03 — Forward pagination with conservative convergence

**Scope:** page assignment and page-state fingerprint helper. Depends on INCR-02. Requires careful review after implementation.

Start repagination from the earliest changed fragment, including preceding keep-with-next dependencies. Reuse a following page only when its incoming state and complete fragment fingerprint agree with the prior layout. Include table continuation/header state, note state and page-number-dependent fields. Initially disable convergence across unsupported tables/notes and fall back to full page assignment there.

**Acceptance:** incremental/full results agree for insertions/deletions at page boundaries, widows, headings and page-number shifts. Tests demonstrate convergence and unchanged downstream reuse. A wrong fingerprint must cause extra work rather than stale pages. Do not extend this task into a custom Markdown parser.
