# Roadmap

Penelope reads a Markdown file the way an ebook reader reads a book: it typesets
what is on screen, keeps that work, and resumes. That pipeline is finished — the
layout cursor, fragment caches, box-painted print pages and page reuse are all in
place and covered by the regression suite. The reasoning is recorded in
[archive/2026-10-17-ebook-reader-roadmap.md](archive/2026-10-17-ebook-reader-roadmap.md).

What remains is fidelity. The reader should render the Markdown people actually
write, not the subset it happens to model.

Penelope will eventually rebase onto `markoff-core` (the sibling project in
`~/dev/Markoff`). Section 1 lists the cheap changes worth making **now**, before
that rebase, because they fix real fidelity bugs and delete semantic divergence.
Section 5 records what the rebase actually requires.

## 1. Interim alignment with markoff-core — now

Both projects already parse footnotes with the same regex-preprocessor technique,
and both are value-snapshot parsers, so most of Penelope's pipeline is not at
risk. What differs is *semantics*. Six small changes remove those differences and
are worth doing whether or not the rebase ever happens.

| Step | Penelope today | Markoff today |
|---|---|---|
| AL-01 Front matter | Rendered as visible body text (`title: …`) | Stripped before parsing, exposed as metadata |
| AL-02 `_emphasis_` | Underline; `MD_FLAG_UNDERLINE` disables `_` emphasis | Emphasis (CommonMark) |
| AL-03 Unreferenced notes | Printed as notes | Dropped (`fn.number > 0`) |
| AL-04 Note markers in copy | Plain-text copy prints a 0-based index | 1-based by first reference |
| AL-05 Link targets | `href` only; titles and wikilink targets discarded | `LinkInfo{Standard, Wiki, Image, Embed}` with a structured target |
| AL-06 HTML blocks | Block HTML and its inner text are deleted | `HtmlBlock` is a block kind with `<img>` attributes |

Steps: AL-01 … AL-06 in [tasks.md](tasks.md#interim-alignment).

## 2. Footnotes — next substantive workstream

**Endpoint.** A note prints at the foot of the page that cites it, numbered the
way the style says, with real Markdown inside it.

**Where it stands.** Footnotes are extracted by a preprocessor and appended as
one endnote section at the end of the document. The placement switch is locked
to endnotes, the numbering restart is locked to per document, and each note body
is flattened into a single text run. Notes have no back-link to their references
and references have no hover.

**Markoff is behind here, not ahead.** Its parser keeps only the first line of a
definition body (`^\[\^label\]:\s*(.+)$`) and `markoff-core` stores notes as a
`FootnoteId → QByteArray` map with no public read accessor. If Penelope gains
multi-paragraph notes with page-bottom placement and then rebases as things
stand, footnotes regress. Two consequences:

- the cheap alignment items above are safe; richer note *storage* is not yet
  worth pushing further in Penelope alone, and
- note fidelity must land in `markoff-parser`/`markoff-core` too — multi-line
  definition bodies and a read accessor — before the rebase is attempted.

Steps: FN-01 … FN-06 in [tasks.md](tasks.md#footnotes).

## 3. Markdown dialect

The parser is configured for CommonMark plus GFM tables, task lists,
strikethrough and permissive autolinks, plus underline, `[[wikilinks]]` and
LaTeX math spans, with PHP Markdown Extra footnotes pre-processed. The losses
are downstream of the parse:

| Gap | Today | Should be |
|---|---|---|
| Math spans | Literal `$…$` text | Typeset. Markoff renders LaTeX in its leaves with vendored jkqtmathtext |
| Raw HTML | Blocks and inline HTML dropped | Keep the bytes; policy below |
| Named entities | 20 names decoded, the rest printed as `&alpha;` | The full HTML entity set. Markoff decodes none, so this is fidelity, not alignment |
| Smart punctuation | None | Curly quotes, en/em dashes, ellipses |
| Link and image titles | Discarded | Hover tooltips (see AL-05) |
| Fence info strings, `)` list delimiters, tight/loose lists | Discarded | Preserved where they change the rendering |
| Inline images outside paragraphs | Dropped in headings, table cells, list and quote text, note bodies | Same media rules in every inline context |
| Definition lists | Not recognised | Not planned: neither Markoff nor MD4C has them |

Steps: MD-01, MD-02, MD-06, MD-07, MD-08 in
[tasks.md](tasks.md#markdown-dialect), plus the already-decomposed MD-04 and
MD-05. MD-03 and the front-matter half of MD-09 are absorbed by AL-05 and AL-01.

## 4. Reading quality, and the deferred performance items

Carried forward from the previous plan and the October audit: selection
granularity for shaped text, PDF text semantics for ligatures and BiDi,
accessibility of the custom-drawn reader, and the performance items that were
explicitly deferred.

`markoff-core` already ships interfaces Penelope has grown by hand —
`FindController`, `SearchEngine`, `ClipboardCodec`, `LinkService`,
`SyntaxHighlightService` — and `markoff-canvas` closed an accessibility arc with
a per-block `QAccessible` tree. Reuse by interface is the cheap path; a
rebase would hand these over wholesale.

Steps: Q-01 … Q-08 in [tasks.md](tasks.md#quality).

## 5. Later: the markoff-core rebase

Not interim work. What the two adoption steps actually cost, from the current
Markoff tree:

- **`markoff-parser` first.** It is the standalone seam: `Markoff::Document`
  from `fromMarkdown()` is a value snapshot with frontmatter, footnotes, links,
  tags and per-block byte ranges. It needs libtree-sitter, its vendored grammar
  and rapidyaml, all linked privately into the parser library. Adopting it
  replaces MD4C and changes coordinates: UTF-8 byte offsets, block-relative
  spans, per-block buffers whose conventions differ (headings include `#`, list
  markers are removed and rebuilt from attributes, code blocks include fences).
  Penelope's line-based source map and `ScrollAnchor` would be re-anchored to
  block ids and byte ranges.
- **`markoff-core` second, and it is not a library drop-in.** A document cannot
  be opened without a `DocumentStorageProvider`, which means the installed
  CollabText Core package; Markoff's CMake fails to configure without it. Core
  itself renders nothing: `MarkdownView` is an abstract contract base, and the
  leaves (canvas, styled, source) live outside it. Accessibility, image and math
  rendering, resource resolution (`Vault::ResourceProvider`), embeds
  (`EmbedRegistry`) and even footnote reading are host- or leaf-supplied.
  Standalone Markoff installs no CMake package today; only a Corbomite
  subdirectory build exports targets.

**Prerequisites before a rebase is worth attempting:** an installed or
exported Markoff target; a read path that does not demand a durable provider;
multi-line note content plus a footnote read accessor in the parser and core;
and a projection layer from per-block buffers to Penelope's box tree. Until
then, Penelope stays on MD4C and adopts the alignment items above so the
projection has less to reconcile.

## Open decisions

- **Raw HTML.** Recommend: keep the bytes (AL-06) and render them as literal
  text, since neither project has an HTML renderer and silently deleting
  content is worse. A filtered subset can come later.
- **Front matter.** Settled by AL-01: mirror `markoff-parser`'s rule, hide it
  from the reader, keep the source untouched.
- **Task-list interactivity.** Clicking a checkbox edits the source: a write
  path. Recommend: not yet.

## Not doing

- A streaming Markdown parser. The first screen may wait for the parse; it is
  the smaller floor.
- Replacing MD4C, HarfBuzz or the PDF writer before the rebase.
- A Markdown-to-HTML renderer.
- Reflowing print view on window resize; that is the PDF-reader behaviour we
  keep.
