# Penelope performance review — 2026-10-04

The performance work was directionally sound, but its measurement and correctness checks were incomplete. Keep the rebuild-loop fix, cheaper preview compression, and compact PDF text operators. The compact operators needed a correctness repair. The next large improvements should address synchronous composition and repeated layout, rather than concentrate on PDF number formatting.

Reviewed commits: `e246282` through `9c2e42f`, on top of the maintenance work. The review's fixes and benchmark improvements are working-tree changes. No major architectural refactor has been implemented in this pass.

## Assessment of the other agent's changes

| Change | Assessment |
| --- | --- |
| Ignore presentation-only editor changes (`e246282`) | Correct and consequential. It prevents palette/highlighting activity from repeatedly invalidating composition. The regression test covers palette, font, genuine edits, and programmatic replacement. Comparing the entire source buffer for every editor notification remains costly for large documents. |
| Stage benchmark (`2f9a726`) | Useful starting point. It separated pipeline stages, but recreated fonts on every iteration and did not record CPU time or individual samples. Its `open` total excluded UI installation, font construction/destruction, and first paint. |
| Hash and edit modes (`19a3a69`) | Hashes are useful for changes intended to preserve PDF bytes, provided `QT_HASH_SEED=0` is set. The edit mode did **not** measure first paint or completed rendering: it classified long `processEvents()` calls as paints. Those can include unrelated work or scheduler delay. |
| Append PDF operators (`ca58ccf`) | Reasonable reduction of temporary byte arrays. It retains Qt coordinate rounding, which matters for output equivalence. `appendCoord()` still constructs a temporary for each number, so the commit title overstates the elimination of temporaries. The claimed 30% gain has not been independently isolated in this review. |
| Preview compression level 1 (`0812071`) | Good tradeoff for transient reader PDFs. Verified substantial CPU savings compared with level 6. Export and print retain default compression. |
| Run-based text operators (`9c7b920`) | Worth keeping: substantially smaller output and faster generation in the optimized experiment below. Width caching avoids the documented repeated FreeType glyph-load mistake. However, the implementation dropped vertical offsets for every glyph after the first. |
| Custom adjustment formatting (`c57f9fd`) | Small, localized optimization. It rounds corrections to two decimals and avoids printf. The large gains below cannot be attributed specifically to this formatter; it was enabled in both operator modes. |
| Sampling profiler (`9c2e42f`) | Useful as a source of hypotheses, insufficient for precise function/stage attribution. It records leaf instruction pointers without call stacks and samples warmup, layout, generation, and cleanup together. It originally assumed Linux meant x86-64 and had shared signal-handler state without atomic synchronization. |

The archived commit descriptions openly record mistakes that were corrected before landing: missing glyph usage in the early benchmark, an expensive uncached width lookup, and instrumentation that reported zero. Those are reasons to distrust intermediate measurements, not evidence that the final implementation still has each abandoned defect.

## Did system load interfere?

At the start of this review, `uptime` reported load averages **28.35 / 29.69 / 32.13** on an Intel i7-1185G7 with **4 physical cores / 8 logical CPUs**. `/proc/pressure/cpu` reported approximately **53%** CPU pressure over the preceding ten seconds and **56%** over a minute. Pressure remained substantial during the experiment; the raw results retain readings before each run.

This establishes that this review was performed on a busy host. It does not reconstruct the exact load during yesterday's experiments. The old profiler outputs and commit descriptions mention load, but do not provide a matched record of scheduler pressure for each historical timing.

Wall-clock measurements can therefore move for reasons unrelated to the patch. CPU time excludes waiting to be scheduled, but still responds to frequency changes, competing memory/cache traffic, and work performed by other threads within the benchmark process. It is an additional signal, not a complete cure for contention.

There were also defects that load cannot explain: omitted vertical positions, missing viewport culling, and the benchmark calling arbitrary event-loop activity “first paint.” The appropriate conclusion is **both**: load plausibly obscured smaller effects, while correctness and instrumentation problems independently affected the work.

## Controlled measurements

Built a separate tree with `RelWithDebInfo` (`-O2 -g -DNDEBUG`), Qt 6.11.2 and GCC 16.2.1. The existing `build/` has an empty `CMAKE_BUILD_TYPE` and no optimization flags in `PenelopeCore`'s compile options. That existing build is unsuitable for drawing conclusions about production speed. I could not establish the build flags for every archived historical executable.

All conditions below used the same optimized binary, synthetic input, installed fonts, warm font manager, and deterministic Qt hash seed. Each condition ran twice, with three measured iterations after warmup in each process. The second sweep reversed condition order. No compilation ran alongside these timing sweeps. The host's unrelated workload remained active.

For the larger document: **377,971 bytes**, 1,665 top-level blocks, 293 default-layout pages. These are six-sample medians, not a claim about every document or font:

| Text operators | Compression | PDF CPU time | PDF wall time | PDF bytes |
| --- | --- | ---: | ---: | ---: |
| Absolute per glyph | 6 | 745.1 ms | 786.4 ms | 2,480,738 |
| Absolute per glyph | 1 | 557.1 ms | 625.7 ms | 3,012,110 |
| Compact runs, with vertical-offset fallback | 6 | 446.3 ms | 488.2 ms | 1,003,156 |
| Compact runs, with vertical-offset fallback | 1 | 358.0 ms | 368.7 ms | 1,346,159 |

At level 1, compact runs reduced PDF-generation CPU time by approximately **36%** and PDF bytes by **55%**. Within compact runs, level 1 reduced generation CPU time by approximately **20%** relative to level 6. The ranges for those larger effects are separated: absolute level-1 CPU samples were 553–581 ms; compact level-1 samples were 346–416 ms. These results support keeping the changes despite contention. They do not independently verify the older three-megabyte-document figures.

The smaller, 94,166-byte input showed the same direction: absolute level-1 generation took a median 138.6 ms of CPU; compact level-1 took 103.5 ms.

With compact preview operators on the larger input, parsing took about **10 ms CPU**, paginated layout about **229 ms CPU**, and generation about **358 ms CPU**. Median combined parse/layout/generation CPU time was **595 ms**. These timings exclude UI installation, full source highlighting and source-editor notifications.

A separate three-sample run measured continuous layout at **228 ms CPU** and loading/rasterizing the first three PDF pages at **27 ms CPU**. That raster measurement is limited to DPR 1, zoom 100%, the synthetic document and an offscreen environment. It is not scroll latency or a high-DPI rendering benchmark.

A fresh-font run increased median layout CPU to **277 ms** on the larger input, versus roughly 229 ms with warm fonts. The smaller input's fresh-font layout was 140 ms versus roughly 59 ms warm. Font startup costs matter, and the original harness conflated cold font loading with repeated edits. There is currently **no shaped-text cache** in `TextShaper`; the old synthetic document's claim about sharing such a cache was inaccurate and has been corrected.

[Raw samples, command arguments and host pressure readings](performance/2026-10-04-results.json) are retained with this report. These comparisons isolate the available operator/compression switches; they are not a full before/after test of all eight commits.

## Corrections made in this review

### Preserve vertical glyph positioning

`PdfBoxRenderer::drawGlyphsCIDFont()` used the first glyph's Y position for the entire TJ array. TJ adjustments only changed X, so subsequent `GlyphRenderInfo::positions[i].y()` values were lost.

Runs with differing Y offsets now fall back to the existing per-glyph absolute matrices. Ordinary constant-baseline runs retain compact output. A controlled test changes the second glyph's vertical offset and compares Poppler raster output; it failed before the fix and passes afterward. The original operator equivalence test now checks vertical positions and dimensions as well as horizontal position, and uses a floating-point maximum instead of truncating drift to an integer.

This does not certify all complex-script extraction behavior. The existing multilingual test plus the explicit offset test establish the repaired positioning contract; a larger typography corpus remains useful.

### Activate continuous-reader viewport culling

`WebViewItem` already binary-searched visible elements, but did not set `ItemUsesExtendedStyleOption`. Qt consequently supplied its whole bounding rectangle as `exposedRect`. The supposedly culled loop traversed the entire document during a viewport paint. Qt documents this behavior in the [QGraphicsItem flag reference](https://doc.qt.io/qt-6/qgraphicsitem.html#GraphicsItemFlag-enum).

The missing flag is now enabled. A real `QGraphicsView` test with 200 linked paragraphs failed before the change and passes afterward: the exposed rectangle is a small fraction of the full document and the paint collects fewer than 50 link rectangles.

Partial repaints introduce a second requirement: clearing all links on each narrow scrolling-stripe paint would make previously painted links lose hit-testing. Hit rectangles are now cached by immutable layout element, reset with a new layout, and looked up near the pointer's Y position. The test also paints a distant stripe and verifies an earlier link remains clickable.

Culling bounds traversal by visible **elements**, not visible lines. One huge code or paragraph block can still require considerable work, and selections still iterate their rectangle list.

### Avoid repeated source presentation work

Rebuilds now skip assigning an unchanged source-editor palette. `MarkdownHighlighter::setPaletteColors()` returns without rebuilding regex rules or rehighlighting the complete source when its colors have not changed. Actual color changes still rehighlight.

### Make the benchmark honest and more useful

- Added process CPU times and JSON output containing every sample, PDF size and experiment settings.
- Added `--warm-fonts` to reuse the same font manager/shaper across measured iterations. Default fresh-font behavior remains available.
- Renamed pipeline “open” timing to `pipeline`: it measures parse + paginated layout + PDF generation, not application startup/paint.
- Renamed edit-loop `firstPaint`/`settled` to `firstBusy`/`lastBusy`, explicitly describing them as heuristics. The observation loop now runs through the configured window instead of declaring completion after a quiet interval. It sleeps between event-loop polls to avoid a busy spin. Real render-completion instrumentation is still needed.
- Moved optional continuous layout after PDF generation so its font-usage reset does not change the paginated PDF being benchmarked.
- Isolated benchmark settings in a temporary directory.
- Limited the built-in sampler to Linux x86-64, made its signal state lock-free atomic, added a configurable output directory (`BENCH_PROFILE_DIR`) and ensured that directory exists. Default output is a process-specific temporary directory. Linked the platform dynamic-loader library explicitly.
- Kept the cheap CTest benchmark smoke case; made the lengthy size curve opt-in with `PENELOPE_PERFORMANCE_TESTS=ON`. A benchmark that merely exits successfully is not a performance regression gate.

The sampler remains an experimental leaf-PC tool. Prefer a profiler with stacks and matching debug symbols for optimization decisions. Some old library “function names” may be nearest exported symbols from stripped binaries rather than the functions executing. The archived attribution script also limited executable address attribution to the first 400 unique addresses; those tiny per-function percentages are not a dependable ranking of application hotspots.

## Recommendations, in priority order

### 1. Stop composing a hidden reader on every source edit

The source editor and reader occupy a stacked widget. The 300 ms edit timer still rebuilds the reader while the source editor is visible. A debounce limits frequency, but does not shorten a long UI-thread stall. Retain the document's dirty composition generation and rebuild when returning to reader view or explicitly exporting/printing. External reload in a visible reader should still refresh it.

If a simultaneous live preview is introduced, make preview scheduling explicit rather than impose that cost on every source-only edit. Capture actual edit-to-preview-install and first-preview-paint signals; arbitrary event-loop durations cannot answer those questions.

### 2. Move composition into a bounded worker pipeline

Introduce a render request containing immutable Markdown, resolved styles, page geometry, settings, base path, language overrides and a per-tab revision. Take the text/widget snapshot on the GUI thread. Perform semantic building, layout and PDF generation in a worker, then install the result on the GUI thread only if its revision still matches.

Use one active job and one replaceable pending request initially. Prioritize the active tab, coalesce resize/style/edit requests and discard superseded results. Cancellation checks between blocks/pages avoid spending seconds finishing obsolete work. Keep the previous reader displayed until its replacement is ready.

Do **not** put the existing shared `m_fontManager` into concurrent jobs: FreeType/HarfBuzz faces and `usedGlyphs` are mutable. Layout boxes also hold raw `FontFace *` pointers. Define a render snapshot that owns the fonts/assets needed by its boxes, or transfer immutable font data/IDs and materialize GUI rendering fonts separately. QObject/widget access remains on the GUI thread. This ownership boundary is the same one needed to extract the renderer for Markoff.

Success criteria: typing/scrolling remains responsive during long composition; stale revisions never install; closing tabs while jobs run is safe; queue size remains bounded. Worker execution improves responsiveness, not necessarily total CPU consumption.

### 3. Reuse semantic documents and shaped paragraphs

A width change should not reread/reparse the source or rerun source highlighting. Cache semantic content by source revision and parser-affecting settings, then cache paragraph shaping independently of line breaking. Cache keys must include the text, resolved font/fallback identities, face revision, size, features and shaping-affecting language/direction settings. Changes in width invalidate line breaking, not all glyph shaping; pure colors should invalidate paint rather than metrics when possible.

A replayed shaping result must restore the glyph-usage set used for PDF subsetting. Font replacement, image changes and hyphenation settings need explicit invalidation. Use bounded caches and measure hit rates. Do not add an unbounded map of every historical editor state.

Also retain GUI `QRawFont` caches across compatible layout replacements. Currently each new `WebViewItem` creates a new `QtBoxRenderer` and discards its font cache.

### 4. Consider rendering paginated preview directly from boxes

Penelope already has a shared box traversal with QPainter and PDF backends. The paginated reader currently lays out boxes, serializes a PDF, loads that PDF into Poppler, then rasterizes it. Continuous view renders boxes directly.

A paginated QPainter backend could display the same layout result and defer PDF serialization to export/print. The measurements put roughly 358 ms CPU into preview serialization on this input, making this a much larger opportunity than another small formatter adjustment. It is a proposed saving, not a measured end-to-end gain.

This is a major refactor because PDF mode currently relies on Poppler for text boxes, search/selection and link behavior. Move those interactions to source/glyph anchors in the layout snapshot and test their behavior before switching the default. Compare direct preview against exported PDF for font fallback, marks, RTL, images, tables, decorations and header/footer fields. Preserve Poppler as an optional exact-PDF preview if that workflow matters.

### 5. Incremental pagination after the snapshot/cache boundaries are stable

Give blocks stable identities and cache width-dependent layout fragments. Reflow from the first changed block, including predecessors affected by keep-with-next or table/footnote constraints. Reuse a following page only once its start state and layout fingerprint converge with the old result. Account for page-number fields and changes that shift downstream numbering.

Do not start with a completely custom incremental Markdown parser. Parsing was only about 10 ms here; layout and PDF generation dominate. Markoff's native block identity would be useful in a future integration, but replacing MD4C alone is not supported as a performance improvement by these measurements.

### 6. Smaller opportunities to validate afterward

Reuse ICU break iterators within an engine/job; consider sharing the emitted PDF width table with run emission rather than loading each used glyph again to discover the same width. Cache table intrinsic measurements where inputs are unchanged. Add line-level culling for exceptionally large continuous blocks. Avoid full `toPlainText()` conversion on every presentation notification, while retaining the rebuild-loop fix.

Changing hinting/font-metric policy is a typography change. Do not switch it for speed without an explicit geometry/raster/extraction comparison and consistent layout/render metrics.

## Reproducing the experiments

```sh
cmake -S . -B /tmp/penelope-perf -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build /tmp/penelope-perf -j 2
ctest --test-dir /tmp/penelope-perf --output-on-failure
QT_QPA_PLATFORM=offscreen QT_HASH_SEED=0 /tmp/penelope-perf/bin/penelope_renderbench \
  --sizes 400,1600 --iterations 3 --warm-fonts --compress-level 1 --run-text \
  --json /tmp/penelope-run.json
```

Repeat with `--no-run-text` and with `--compress-level 6`, alternating and reversing order. Record load/CPU pressure and avoid building concurrently. Use `--file` for representative documents, `--continuous`/`--raster` for separate stage studies, and omit `--warm-fonts` to examine fresh-font layout. Hash comparisons require the same hash seed, fonts/settings and operator/compression mode; changing compression or the text-operator structure should change the hash.

## Validation

The new vertical-offset and viewport-culling cases were each observed failing before their respective fixes. Both pass afterward, together with the existing multilingual operator-equivalence case. The optimized application, regression suite and benchmark build successfully. All three CTest entries pass (`appstreamtest`, `penelope_regressions`, and `penelope_renderbench_smoke`). The sampler smoke run produced 161 samples in its configured directory. Normalized PDF hashes were stable across two separate clock times with `QT_HASH_SEED=0`. `git diff --check` passes. The optional long CTest size curve was not run; its two representative sizes were exercised directly in the recorded timing experiments.
