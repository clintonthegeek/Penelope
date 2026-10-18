# Benchmark

How reader performance is measured. The first recorded run, its samples and its
analysis are in
[archive/2026-10-17-long-document-benchmark.md](archive/2026-10-17-long-document-benchmark.md).

Time to a first readable page and time for one scroll step are different
questions, and a single "render" number mixes them. `--views` separates them.

| Question | Recorded as |
|---|---|
| How long until the reader has a result? | `ready`, `parse`, `layout`, `pdf` for a cold composition request |
| Does a width change avoid parse and shaping? | a second request at width + 120 pt: `reuse`, `hits`, `misses` |
| Is web scrolling proportional to the viewport or the document? | paint of a 900 px viewport at the top, the middle, and a 120 px stripe, then the same viewport with the whole item exposed |
| Does the graphics view add its own cost? | `grabTop`, `grabMid` |
| Is print view slow because it builds one item per page? | `popplerLoad`, `pageWalk`, `sceneItems` |
| Is the first print page waiting on rasterization? | `first`, `mid`, `last`, `firstAgain`, `cacheTwo`, `cacheHit` |

`ready` measures the worker, submit to result; it does not include table-of-
contents construction, source highlighting, or the first on-screen raster.

## How to run it

Use an optimized build. A `build/` tree configured without a
`CMAKE_BUILD_TYPE` is not a useful measurement.

```sh
cmake -S . -B /tmp/penelope-viewbench -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build /tmp/penelope-viewbench -j 6 --target penelope_renderbench
QT_QPA_PLATFORM=offscreen QT_HASH_SEED=0 /tmp/penelope-viewbench/bin/penelope_renderbench \
  --views --sizes 400,1600 --shape all --iterations 3 \
  --json /tmp/penelope-views.json
```

`--shape` picks the fixture:

- `prose` — many short paragraphs, lists and small tables; the count is
  paragraphs.
- `code` — one fenced C++ block; the count is lines, highlighting on.
- `wall` — one paragraph of repeated sentences; the control for "many lines,
  one element, one style run".

`--iterations` repeats the paint and grab samples and prints the median;
composition is one sample per document. `--no-hyphenate` turns off dictionary
and justified hyphenation, which are both on by default; a run that failed to
initialize the bundled dictionaries is visible as `soft_hyphens: 0` in the JSON.

## Reading the numbers

- Record `/proc/loadavg` and `/proc/pressure/cpu`. A busy host moves wall time;
  the bench stores both in the JSON `host` object.
- Read effects that are many times larger than run-to-run noise, not small
  percentages from one sample.
- Do not put thresholds in CTest. Assert structure instead: how many blocks were
  laid out, whether a cache hit, whether a cancelled request returned early.
- Not covered: high-DPI screens, a real window resize of `DocumentView`, typing
  in the source editor, or table-of-contents construction.
