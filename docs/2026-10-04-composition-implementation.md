# Asynchronous composition implementation — 2026-10-04

Implemented the scheduling, ownership and shaping-cache foundations from the performance review. The modern PDF and continuous readers now compose in a dedicated worker. The legacy QTextDocument reader has subsequently been removed; both reader modes now use this worker. Explicit PDF/RTF/print workflows remain synchronous.

## Shipped behavior

- Opening a modern-reader document schedules its composition and returns to the event loop. The previous reader stays displayed during subsequent jobs.
- One job runs at a time, with at most one pending replacement. A newer request cancels the current job and replaces the pending request. There is no growing queue of editor states.
- Source view cancels pending reader work and does not compose its hidden reader after edits. Returning to a reader schedules the latest source. Export/print build from their own fresh source snapshot.
- Source changes invalidate an active request immediately, before the 300 ms reader debounce. Theme/mode changes, tab switches, source-mode entry and target-tab closure cancel superseded work. Installation also verifies target identity, source revision, composition revision and current continuous width.
- Scroll and zoom are captured when the result installs, preserving navigation performed while a job was running. Initial per-file/session page and scroll state is retained until the first snapshot exists. Composition generation is stamped only after installation, not when the job starts.
- The source replacement helper now suppresses programmatic editor notifications and records Qt's normalized text. It increments a monotonic source revision, including reloads.

## Ownership and thread boundary

[CompositionRequest](../src/render/compositioncontroller.h) contains value copies of source, style definitions, page geometry, language overrides, image/typography/highlighting/table settings and revisions. Requests do not refer to widgets or GUI-owned QObjects. A worker-local StyleManager/ContentBuilder and typography objects build the semantic document.

The worker owns a persistent FontManager/TextShaper context, separate from GUI export fonts. FontManager is now an ordinary C++ resource owner: it never needed QObject signals, parenting or thread affinity. FreeType/HarfBuzz state and glyph usage are mutated exclusively by the worker. GUI continuous painting accesses immutable raw font bytes and immutable Hershey artwork, not the worker's mutable FreeType glyph state.

Results hold shared ownership of that font context. Continuous items and DocumentView retain the owner, so raw FontFace pointers in layout boxes remain valid after replacement, tab closure or controller destruction. No result is installed while the wrong tab/source revision is current. The controller cancels and joins its thread at shutdown; it never force-terminates a renderer.

Settings reads in worker paths are replaced with explicit ContentBuilder/Layout::Engine options. Code-span repositories are local to the calling thread, with an explicit theme/enable flag. Hyphenation dictionary discovery and Hershey registry loading use `call_once` so concurrent reader/export initialization cannot mutate shared registries simultaneously.

Cancellation checks occur in parser callbacks, between top-level/nested blocks, table rows, page assignment and PDF pages. Individual HarfBuzz shaping calls, image decoding and font subsetting are still indivisible; cancellation is cooperative, not an interruption of those libraries. Shutdown can wait for an indivisible operation to finish.

## Cache behavior

TextShaper has an LRU cache with an approximate 8 MiB cost limit. Keys include exact text, style-run boundaries, font family/weight/italic, size, features and fallback identity. Cache hits replay used glyphs after `resetUsage()`, retaining valid PDF subsets. Changing fallback clears the cache. `setCacheLimit()`, hit/miss counters and cost are exposed for controlled tests and tuning. Cost accounting estimates payload size rather than allocator overhead; it is not an exact RSS ceiling.

The worker keeps one semantic document for the latest successful parse. Width and reader-mode changes can reuse it when source, base path, semantic revision, typography/image settings and language overrides match. Style-map changes must advance `semanticRevision`; MainWindow uses its existing composition generation for this contract. Page/style changes currently also invalidate semantics conservatively.

Sources containing Markdown image syntax bypass semantic caching so external asset replacement is read again. Sources larger than 8 MiB also bypass it. These are conservative policies, not a general dependency tracker. Large documents can exceed the shaping-cache budget and receive fewer hits; retained statistics make that visible.

## Diagnostics and validation

`MainWindow::documentRendered(DocumentTab *)` identifies completed GUI installation, including semantic/source-map updates. It does not mean the first PDF raster has finished. The benchmark's `--open` and `--rebuild` modes now report this event as `ready`, or `timeout` if it was not observed. Rebuild mode edits source and returns to the requested reader; hidden-source editing deliberately has no preview completion event.

New regression coverage checks request coalescing, newest-result delivery, explicit cancellation, width-only semantic and shaping reuse, glyph-usage replay and PDF extraction after cache hits, cache-size eviction and key changes, hidden-reader deferral, cancellation on source-mode entry, tab closure with pending work, and painting after the controller has been destroyed. Existing reader mode-switch tests now wait for asynchronous installation.

The worker, cache, and ownership boundary above stay. The [ebook-reader roadmap](2026-10-17-ebook-reader-roadmap.md) is the plan for what the worker should compute: a resumable layout of the visible range, with export still running that cursor to the end. The [October task list](2026-10-04-performance-tasks.md) is superseded by that roadmap. This implementation preserves the existing PDF reader's selection, search and export behavior until those roadmap tasks replace the interactive preview.

## Measurement limits

A post-change optimized five-sample run on the 94,166-byte synthetic input
measured median layout CPU time of 37.9 ms. The earlier review measured roughly
59 ms under different host conditions. This is an encouraging smoke result,
not a matched A/B speedup claim. [Raw post-change samples](performance/2026-10-04-cache-after.json)
are retained. Cache-reuse assertions are deterministic regression tests.

The standalone window benchmark showed environment-sensitive startup and
reported installation timeouts with a one-second observation window. A traced
small-document run completed and delivered both installation signals. Those
runs are not used as evidence of an end-to-end latency improvement. Worker
separation, bounded queuing and revision/lifetime behavior are verified by the
integration tests; representative interactive latency measurements remain
PERF-01 follow-up work.

## Final checks

The optimized application and both test executables build successfully. All
three CTest entries pass; QtTest reports **42 passed, 0 failed, 0 skipped**.
This includes saved-page restoration after asynchronous opening. Documentation
links and `git diff --check` pass. Changes remain in the working tree.
