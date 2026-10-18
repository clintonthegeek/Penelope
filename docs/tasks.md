# Tasks

Executable steps for [the roadmap](roadmap.md), in order. One step per change.

Each step names the files it touches, what to do, what not to do, the test slot
to add, and the observable acceptance. Steps with an id are ready to execute.
Constructs listed without an id still need a design pass.

## Rules

- Read the named files and the tests that already cover them first.
- Assert structure: how many blocks were laid out, whether a cache hit, whether
  a partial result matches the same prefix of a full result. Never assert
  wall-clock milliseconds in CTest.
- Use a `RelWithDebInfo` build for any speed comparison, and
  `penelope_renderbench` for the numbers ([benchmark.md](benchmark.md)).
- Keep widgets and `PenelopeSettings` out of worker code. Cooperative
  cancellation only: check between units of work, never kill a thread, never
  cache a partial shape.
- Run the whole suite before calling a step done:

```sh
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

## Footnotes

### FN-01 — Note bodies keep their Markdown structure

**Touch:** `src/markdown/footnoteparser.cpp`, `src/model/contentbuilder.{h,cpp}`,
`src/model/contentmodel.h`, `src/layout/layoutengine.cpp`,
`src/export/contentrtfexporter.cpp`, `src/model/contentplaintext.cpp`,
`tests/regressiontest.cpp`.

**Do:**

1. Replace `Content::Footnote::content` (`QList<InlineNode>`, today one
   `TextRun`) with `QList<BlockNode> blocks`.
2. Add `QList<BlockNode> ContentBuilder::buildFragment(const QString &markdown)`
   that runs the existing MD4C callbacks into a scratch `Content::Document` and
   returns its blocks. Block ids must come from the same counter as the body so
   ids stay unique (see `ebook03BuildAssignsUniqueIds`).
3. In `build()`, call `buildFragment` on each definition's body instead of
   appending a `TextRun`.
4. `layoutFootnoteSection` lays out the note's blocks through the ordinary block
   dispatch, indented under the note label, and keeps `splitFootnoteSection`
   able to split between notes.
5. Plain text, RTF and PDF walk the blocks as they do for the body.

**Do not:** reparse the whole document per note. Do not renumber notes.

**Test:** slot `fn01FootnoteBlocks`. A note whose body holds a paragraph, a
two-item list, `*emphasis*` and an indented code line produces those block
types inside the footnote section, and the PDF text of that page contains the
list item text.

**Done when:** that slot passes, `footnotesPreserveTextAndLines`,
`footnoteLiterals`, `landscapePdfAndFootnotes` and
`contentPlainTextPreservesVisibleContent` still pass, and a note long enough to
cross a page boundary splits between its blocks.

### FN-02 — Notes know which references cite them

**Touch:** `src/model/contentmodel.h`, `src/model/contentbuilder.cpp`,
`src/layout/layoutengine.cpp`, `src/canvas/hitindex.cpp` (read index),
`tests/regressiontest.cpp`.

**Do:**

1. Give `Content::Footnote` a `QList<Content::SourceRange> references` (or the
   laid-out rects, whichever the layout can fill) and fill it while scanning
   body text for `[^label]`.
2. Mark a reference that repeats an earlier one (`FootnoteRef::isRepeat`) and
   keep the same printed marker.
3. Carry the note index on `FootnoteRef` so the layout can find the note.

**Do not:** add a back-link marker to the rendered output yet; FN-06 does that.

**Test:** slot `fn02ReferenceAnchors`. A note cited three times, once from a
heading and once from a table cell, records three references, two of them
marked as repeats, and all three render the same marker.

**Done when:** the slot passes and Markdown copy of a paragraph with a repeated
reference is unchanged.

### FN-03 — Numbering moves into the layout

**Touch:** `src/model/contentbuilder.cpp`, `src/layout/layoutengine.{h,cpp}`,
`src/model/contentplaintext.cpp`, `src/export/contentrtfexporter.cpp`,
`src/pdf/pdfboxrenderer.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Stop formatting note markers in the builder. `Footnote` and `FootnoteRef`
   keep the note index; the printable label is resolved at layout time through
   a `noteIndex → label` map carried on the layout result and the box snapshot.
2. Assign labels in document order of first reference, exactly as today, so
   output does not change in this step.
3. Plain text and RTF read the resolved labels instead of `i + 1`, so the
   configured format, prefix and suffix reach the clipboard.

**Do not:** change any rendered output. This is a refactor; `restart` stays
per document.

**Test:** slot `fn03NumberingIsLayoutOwned`. The same document renders identical
markers to the current build for Arabic, Roman and asterisk formats with a
non-default `startNumber` and prefix, and the plain-text export of a paragraph
with a reference contains the formatted marker rather than the raw index.

**Done when:** the slot passes and every existing footnote test still passes.

### FN-04 — Notes print at the foot of the page that cites them

**Touch:** `src/layout/layoutengine.cpp` (page assembly, `layoutNextPage`),
`src/style/footnotestyle.h` (`asEndnotes`), `src/widgets/footnoteconfigwidget.cpp`,
`src/pdf/pdfgenerator.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. When `FootnoteStyle::asEndnotes` is false, reserve a band at the bottom of a
   page: separator rule, then the notes cited by blocks placed on that page, in
   marker order, using the note's own blocks (FN-01).
2. Build the page in bounded passes: lay out the body into the full content box,
   collect the notes cited by the blocks that landed on the page, measure the
   band, and if the band does not fit, reflow with the band reserved. Cap the
   passes (four is enough); a page that will not settle keeps the notes it can
   and moves the rest to the next page's band.
3. Notes that do not fit continue on the next page's band, not at the end of the
   document. A note whose reference is on page *n* never appears before page
   *n*.
4. Continuous (web) view has no pages: it keeps the endnote section at the end of
   the column.
5. Enable the placement control in the footnote dialog and delete the
   "Page-bottom placement is not available." tooltip.

**Do not:** split a single note's paragraph across the band boundary if a whole
note fits on the next page instead. Do not let the band exceed a third of the
content height.

**Test:** slots `fn04PageBottomNotes` and `fn04NoteOverflow`. The first builds a
document whose first page cites one note and asserts the note text is on that
page and that the body height is smaller than an identical page without notes.
The second puts thirty citations on one page and asserts the later notes appear
in the band of the following page while page one still shows its first notes.

**Done when:** both slots pass, the PDF export of the same document extracts the
note text on the same page as its reference, and `landscapePdfAndFootnotes`
passes with the default (endnote) placement.

### FN-05 — Numbering can restart on each page

**Touch:** `src/layout/layoutengine.cpp`, `src/style/footnotestyle.h`,
`src/widgets/footnoteconfigwidget.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Implement `FootnoteStyle::RestartMode::PerPage`: the marker counter resets at
   each page break, and references are numbered by the page they land on.
2. A reference on page *n* to a note first cited elsewhere still uses page *n*'s
   numbering, and the note prints in page *n*'s band.
3. Enable the restart combo and remove the "Notes currently use per-document
   numbering." tooltip and the forced index in `loadFootnoteStyle`.

**Do not:** change per-document numbering when the mode is `PerDocument`.

**Test:** slot `fn05PerPageNumbering`. A two-page document with a citation on
each page numbers them 1 and 1 in per-page mode, 1 and 2 in per-document mode,
and the band of each page shows the marker its own references use.

**Done when:** the slot passes and the theme round-trip keeps the mode.

### FN-06 — References and notes point at each other

**Touch:** `src/canvas/documentview.cpp`, `src/layout/hitindex.cpp`,
`src/layout/layoutengine.cpp`, `tests/regressiontest.cpp`.

**Do:**

1. Draw a back-link marker at the start of each note's band entry and give it a
   hit rectangle that scrolls to its first reference (reuse the link hit index
   and `DocumentView`'s scroll machinery).
2. Extend the hover hint to footnote references: hovering a marker shows the
   note's plain text, as link hover shows the target.

**Do not:** add editing of the source from a note; that is a write path.

**Test:** slot `fn06FootnoteHoverAndBacklink`. The hit index reports the note
band and the reference as separate regions, a click on the back-link moves the
view to the reference's page, and the hover hint for a reference contains the
note's text.

**Done when:** the slot passes and hovering a plain link is unchanged.

## Markdown dialect

The parser already emits these spans; the model and renderers drop them.

### MD-04 — Inline images work in every inline context

**Touch:** `src/layout/layoutengine.cpp` (`collectInlines`, `layoutHeading`,
`layoutTable`, `layoutList`, `layoutBlockQuote`, `layoutFootnoteSection`),
`tests/regressiontest.cpp`.

**Do:** give `collectInlines` a real `InlineImage` arm so a heading, table cell,
list item, quote or note body that contains an image produces the same
`BlockBox::ImageBlock` a paragraph does, with the paragraph path's
scale-and-cap rules. Keep the `[Image: alt]` fallback for missing bytes.

**Do not:** change the paragraph path's current paragraph/image/paragraph
splitting. Do not resize an image to fit a table cell narrower than the image
without recording the scale on the box.

**Test:** slot `md04InlineImagesAwayFromParagraphs`. A heading, a table cell and
a list item each containing a small PNG produce an image box with a non-zero
scaled size, and a missing file still produces the alt-text run.

**Done when:** the slot passes and `mediaAndImagePreference` is unchanged.

### MD-05 — Decode the entities Markdown actually uses

**Touch:** `src/model/contentbuilder.cpp` (`resolveEntity`),
`src/markdown/` (new table file if that keeps the builder small),
`tests/regressiontest.cpp`.

**Do:** replace the twenty-name switch with the full HTML/W3C named-entity
table, keeping the existing numeric and hex paths. Names are matched
case-sensitively; entities without a trailing semicolon are not decoded (MD4C
already only accepts `&name;`).

**Do not:** decode entity text inside code spans or code blocks.

**Test:** slot `md05NamedEntities`. `&alpha;`, `&eacute;`, `&hellip;`,
`&notin;`, `&#8230;` and a name that does not exist decode as expected, and the
literal text inside a code span survives untouched.

**Done when:** the slot passes and `numericEntityUnicode` is unchanged.

### Not yet decomposed

| Id | Outcome |
|---|---|
| MD-01 | Math spans become a model node and are typeset, then exported to PDF, RTF and the clipboard. Needs a renderer decision (the sibling Markoff project typesets with jkqtmathtext). |
| MD-02 | A recorded policy for raw HTML blocks and inline HTML, implemented: drop, literal, or filtered subset. See the roadmap's open decisions. |
| MD-03 | Wikilink targets reach the model and resolve to an internal anchor or a file. |
| MD-06 | Optional smart punctuation: curly quotes, en/em dashes, ellipses. Must not touch code spans, code blocks or attributes. |
| MD-07 | Link and image titles become hover tooltips. |
| MD-08 | Fence info strings, `)`-delimited ordered lists and tight/loose list spacing are preserved where they change rendering. |
| MD-09 | Definition lists, and a front-matter policy. Both are preprocessor work in the footnote parser's style. |

## Quality

Carried forward; none of these are decomposed yet.

| Id | Outcome |
|---|---|
| Q-01 | Character-accurate selection for shaped text, so styled copy stops including a whole overlapping block. |
| Q-02 | PDF text semantics for ligatures and BiDi: cluster-aware mappings and a multilingual fixture. |
| Q-03 | Read-only accessibility: expose the document and text navigation to assistive technology. |
| Q-04 | PERF-03 — reuse the ICU break iterator across blocks. Measure after the tall-block slicing of EBOOK-02. |
| Q-05 | PERF-04 — image documents bypass the semantic cache; give them a path that keeps the first screen lazy. |
| Q-06 | PERF-05 — retune the shaping-cache limit against a long fence, not a large prose file. |
| Q-07 | PERF-06 — share `QRawFont` across web items. |
| Q-08 | PERF-07 — suppress palette notifications that cause needless rebuilds. |
