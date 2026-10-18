# Long-document web and print benchmark — 2026-10-17

Opening a long Markdown file is slow in both readers. Scrolling is a separate question, and it is slow only for some document shapes. This note describes a benchmark that separates those costs, records one run, and names the changes that match the measurements.

The raw samples are in [performance/2026-10-17-view-results.json](performance/2026-10-17-view-results.json).

## What to measure

Time to first readable page and time for one scroll step are different. A single "render" number mixes them.

| Question | Where the time goes | How this benchmark records it |
| --- | --- | --- |
| How long until the reader has a result? | Worker parse, layout, and, for print view, PDF generation | `ready`, `parse`, `layout`, `pdf` on a cold `CompositionController` request |
| Does a width change avoid parse and shaping? | Semantic cache and shaping cache | A second request: web width plus 120pt, or print orientation changed to landscape. `reuse`, `hits`, `misses` |
| Is web scrolling proportional to the viewport or to the document? | `WebViewItem::paint` | A 900px viewport at the top, the middle, and a 120px stripe, then the same viewport with the whole item passed as `exposedRect` |
| Does the graphics view add its own cost? | `QGraphicsView` viewport grab, including a scroll to the middle | `grabTop`, `grabMid` |
| Is print view slow because it builds one item per page? | Poppler load, opening every page for its size, creating every `PdfPageItem` | `popplerLoad`, `pageWalk`, `sceneItems` |
| Is the first screen of print view waiting on rasterization? | Poppler `renderToImage` of the first, middle, and last page, then the first page again, then `RenderCache` for the first two pages | `first`, `mid`, `last`, `firstAgain`, `cacheTwo`, `cacheHit` |

`ready` is submit-to-result on the worker. It ends when the snapshot exists. It does not include table-of-contents construction, source highlighting, or the first on-screen raster. Print raster is measured afterwards. A timeout is reported as `timeout` and the process exits non-zero. It is not stored as a time of zero.

The older pipeline mode (`penelope_renderbench` without `--views`) still times parse, paginated layout, PDF, and an optional three-page raster. Its `--hyphenate` flag only flips a layout switch. It does not load a hyphenation dictionary. `--open` and `--rebuild` still report snapshot installation and event-loop gaps. They do not identify a paint. Use `--views` for the reader questions above.

## How to run it

Use an optimized build. The default `build/` tree in this checkout has an empty `CMAKE_BUILD_TYPE` and is not a useful speed measurement.

```sh
cmake -S . -B /tmp/penelope-viewbench -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build /tmp/penelope-viewbench -j 6 --target penelope_renderbench
QT_QPA_PLATFORM=offscreen QT_HASH_SEED=0 /tmp/penelope-viewbench/bin/penelope_renderbench \
  --views --sizes 400,1600 --shape all --iterations 3 \
  --json /tmp/penelope-views.json
```

`--views` requires `--file` or `--sizes`. `--sizes` is a comma-separated list of counts. `--shape` is `prose`, `code`, `wall`, or `all`.

- `prose` is the existing synthetic document: many short paragraphs, lists, and small tables. The count is the number of paragraphs.
- `code` is one fenced C++ block. The count is the number of lines. Highlighting is on, which is the application default.
- `wall` is one paragraph of repeated sentences. The count is the number of sentences. It is the control for "many lines, one element, one style run."

`--iterations` repeats the paint and grab samples. The median is printed. Composition is one sample per document. `--no-hyphenate` turns off both dictionary hyphenation and justified hyphenation. The default matches the application: both are on, and the bench initializes the bundled `dicts` and `fonts` resources. Without that initialization a static-library build reports hyphenation as on while inserting no soft hyphens. The JSON field `soft_hyphens` is taken from the semantic document, so a run that missed the dictionary is visible.

`--window-ms` is the per-request timeout in this mode. The default is 180000. `--zoom` scales print rasterization. The recorded run uses zoom 1 and device pixel ratio 1.

Record `/proc/loadavg` and `/proc/pressure/cpu`. The bench prints both and stores them in the JSON `host` object. Wall time moves when the machine is busy. CPU time is not a substitute for a quiet host. Effects that are many times larger than the run-to-run noise can still be read from one sample.

This run does not measure a high-DPI screen, a real window resize of `DocumentView`, typing into the source editor, or table-of-contents construction. A 2x display rasterizes about four times as many pixels as the print numbers below.

## Recorded run

RelWithDebInfo, GCC 16.2.1, the system Qt, 8 logical CPUs. Load average at the start of the main sweep was 1.63. CPU pressure `some avg10` was 0.02. Composition figures are one sample. Paint figures are the median of three warm paints after one cold paint.

Soft hyphens were present in prose and wall output. The code fixture is identifiers and operators, and its semantic text did not contain soft hyphens.

### Prose: many small elements

| Paragraphs | Bytes | Web ready | Web parse | Web layout | Print ready | Print layout | Print PDF | Pages | Width shape misses |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 400 | 94,166 | 69 ms | 16 ms | 52 ms | 113 ms | 46 ms | 51 ms | 37 | 0 |
| 1,600 | 377,971 | 179 ms | 39 ms | 140 ms | 374 ms | 159 ms | 171 ms | 147 | 1,665 |
| 6,400 | 1,516,298 | 759 ms | 139 ms | 620 ms | 1,443 ms | 619 ms | 673 ms | 586 | 6,657 |

Width requests reused the semantic document (`reuse=yes`, parse under 3 ms). At 400 paragraphs the width pass also had zero shaping misses, and web layout fell from 52 ms to 25 ms. At 1,600 and 6,400 paragraphs the width pass missed about as many shapes as the cold pass, and web layout stayed at 143 ms and 577 ms.

The same 1,600-paragraph file with `--no-hyphenate` parsed in 6 ms instead of 39 ms. Layout stayed about 146 ms. Hyphenation is visible in parse time and is smaller than layout or PDF generation.

Warm web paint of a 900px viewport stayed at about 1 ms from 400 through 6,400 paragraphs. The middle and the 120px stripe matched the top. Passing the whole item as `exposedRect` took 18 ms, 71 ms, and 278 ms. The viewport grab matched the direct paint. Opening every Poppler page and creating every page item took 3.2 ms and 0.1 ms at 586 pages. Rasterizing one page took 7 to 10 ms, and rasterizing it again cost the same. `RenderCache` then returned the first page on the second lookup (`cacheHit=yes`).

### One element: highlighted code and a plain paragraph

Width-pass layout is the fair layout number. The cold pass also loads fonts.

| Fixture | Lines | Width layout | Shape hits on width | Stripe paint | Full-item paint | Print PDF |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| code | 400 | 30 ms | 1 | 6.4 ms | 6.8 ms | 25 ms |
| code | 1,600 | 485 ms | 0 | 27 ms | 27 ms | 63 ms |
| code | 3,200 | 2,310 ms | 0 | 56 ms | 55 ms | 115 ms |
| wall | 343 | 6.2 ms | 1 | 6.4 ms | 6.7 ms | 15 ms |
| wall | 1,377 | 28 ms | 1 | 27 ms | 27 ms | 55 ms |

A 3,200-line highlighted block is ready in web view after 2.4 seconds. Print view is ready after 2.5 seconds, of which PDF generation is 115 ms. The same line count in a single plain paragraph stays near a straight line: four times the lines, about four times the layout time, and the one shaped run is cached.

From 400 to 1,600 code lines, width layout grew 16 times. From 1,600 to 3,200 lines it grew 4.8 times. An eightfold increase in lines, from 400 to 3,200, grew layout about 76 times. A pure square would grow 64 times. The wall control grew about 4.5 times for a fourfold increase in lines.

Web paint of that code block does not get cheaper for a short stripe. At 3,200 lines a 120px stripe, a 900px viewport, and the unculled item are all about 55 ms. The plain wall behaves the same way: 1,377 lines cost 27 ms wherever the viewport sits. Print raster of one page of the same code block stays around 10 ms, because pagination has already split the block into page fragments of at most a few dozen lines.

## What the numbers pick out

### 1. Highlighted code and other single tall elements are laid out as one unit

`Engine::layoutCodeBlock()` builds one inline list for the whole fence and calls `breakIntoLines()` once. Syntax highlighting turns each line into many style runs. `resolveStyleAt()` walks that list from the beginning for every shaped run. A plain paragraph is one style run, and the wall fixture shows that a similar line count is then cheap.

The shaping cache stores one entry per `shape()` call. A 400-line fence fits in the 8 MiB cache and the width pass hits it. A 1,600-line fence does not: the width pass records one miss and zero hits, so the block is reshaped on every width change. Qt's `QCache` refuses an entry whose cost exceeds the limit, and the cost includes the serialized key plus every glyph.

This cost is paid before either reader can paint. It is the reason a long fenced block is slow in web view and in print view.

A line-oriented code layout would match the way code is read, keep each shaped entry small enough to cache, and give the painter a line list it can clip. An index for `resolveStyleAt()` is the smaller change if the single-block layout stays. Separate the two with a timer around `shape()` and around the rest of `breakIntoLines()` before rewriting the line breaker. The wall-versus-code table is the regression check: code width layout should move toward the wall's slope.

### 2. Web paint clips by element, so a tall element is drawn in full

`WebViewItem::paint` binary-searches to the first visible element and stops at the first element below the viewport. Inside an element, `BoxTreeRenderer` draws every line. The prose fixture has at most 8 lines in an element, so a warm viewport stays near 1 ms at 1.5 MB. The code and wall fixtures have one element, so a scroll stripe costs the same as drawing the block.

Losing `ItemUsesExtendedStyleOption` would make prose hit the unculled column instead. At 6,400 paragraphs that is 278 ms against a 1 ms culled paint. The existing viewport-culling regression test guards the flag. The stripe-versus-unculled comparison in this benchmark guards line culling, which that test does not cover.

After line culling, a viewport of a 3,200-line block should fall to the cost of the lines that actually intersect it. At the measured rate that is about a millisecond, which is what prose already achieves.

### 3. Print view waits for a full PDF, then rasters one page quickly

At 6,400 paragraphs, print `ready` is 1.44 seconds: 151 ms parse, 619 ms layout, 673 ms PDF. Poppler load, the all-page size walk, and scene items add about 6 ms. One page then rasters in under 10 ms, and the render cache hits on the second lookup.

The PDF is half of a large prose open, on top of a layout that web view also pays. Direct page painting from the boxes already produced for layout would let the first page appear without that serialization. Export and print can keep generating a PDF. The page-item loop is not the open-time problem at 586 pages.

A second Poppler raster of the same page is not cheaper. The cache in front of it is what makes a repeat paint cheap. That part is working.

### 4. The shaping cache covers a modest prose file and misses a long one

Semantic reuse on a width change is working: parse drops to about 0, and `reused_content` is true. Shaping reuse stops between 400 and 1,600 varied paragraphs. The cold prose pass already reports thousands of hits because the fixture repeats four sentences. Those hits are repeated text, not a warm cache. The width-pass miss count is the interesting one, and at 1,600 paragraphs it matches the cold miss count.

Raising the 8 MiB limit is a measurement, not a guess. The bench should grow a cache-limit sweep before a new default is chosen. A long code fence will not benefit from a larger limit until it is shaped in pieces smaller than the limit. An entry that is bigger than `maxCost` is never stored.

### 5. Hyphenation belongs in the parse number

With the dictionary loaded, a 1.5 MB prose file spent 139 ms in parse and 620 ms in layout. Turning hyphenation off cut the 378 KB parse from 39 ms to 6 ms and left layout essentially unchanged. Keep it in the default bench, because the application default is on. Do not expect it to move scroll time or the code-block layout curve.

## What this run does not ask for

Another pass over PDF number formatting does not touch the 2.3 second code layout or the 1 ms prose scroll. The all-page Poppler walk is real and grows with page count, and at 586 pages it is still a few milliseconds. `QRawFont` construction shows up as a cold paint a few milliseconds above the warm paint on prose. It is not the 55 ms code-block stripe.

Interactive checks still worth adding, as the reader grows a layout cursor, are a real `DocumentView` width change and a source-editor keystroke that returns to the reader. Those paths call the same worker measured here. They also build the table of contents on the GUI thread, which this bench does not time.

The response to these measurements is the [ebook-reader roadmap](2026-10-17-ebook-reader-roadmap.md). Use `--views` as the manual check for each phase there. Do not treat the October follow-up list as the plan.
