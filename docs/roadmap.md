# Roadmap

Penelope reads a Markdown file the way an ebook reader reads a book: it typesets
what is on screen, keeps that work, and resumes. That pipeline is finished — the
layout cursor, fragment caches, box-painted print pages and page reuse are all in
place and covered by the regression suite. The reasoning is recorded in
[archive/2026-10-17-ebook-reader-roadmap.md](archive/2026-10-17-ebook-reader-roadmap.md).

What remains is fidelity. The reader should render the Markdown people actually
write, not the subset it happens to model.

## 1. Footnotes — next

**Endpoint.** A note prints at the foot of the page that cites it, numbered the
way the style says, with real Markdown inside it.

**Where it stands.** Footnotes are extracted by a preprocessor and appended as
one endnote section at the end of the document. The placement switch is locked
to endnotes, the numbering restart is locked to per document, and each note body
is flattened into a single text run — emphasis, lists and code inside a note are
literal characters. Notes have no back-link to their references and references
have no hover.

**Steps:** FN-01 … FN-06 in [tasks.md](tasks.md#footnotes).

## 2. Markdown dialect

The parser is already configured for CommonMark plus GFM tables, task lists,
strikethrough and permissive autolinks, plus underline, `[[wikilinks]]` and
LaTeX math spans, with PHP Markdown Extra footnotes pre-processed. The losses
are all downstream of the parse:

| Gap | Today | Should be |
|---|---|---|
| Math spans | Rendered as literal `$…$` text | Typeset |
| Raw HTML | Blocks and inline HTML silently dropped | A recorded policy: literal text, filtered subset, or dropped with a marker |
| Wikilinks | Target discarded, label only | A resolvable target |
| Inline images | Drawn in paragraphs, dropped in headings, table cells, list and quote text, and note bodies | Same media rules in every inline context |
| Named entities | 20 names decoded, the rest printed as `&alpha;` | The full HTML entity set |
| Smart punctuation | None | Curly quotes, en/em dashes, ellipses |
| Link and image titles | Discarded | Hover tooltips |
| Fence info strings, `)` list delimiters, tight/loose lists | Discarded | Preserved where they change the rendering |
| Definition lists, front matter | Not recognised | A recorded policy per construct |

**Steps:** MD-01 … MD-09 in [tasks.md](tasks.md#markdown-dialect). Only MD-04 and
MD-05 are decomposed so far; the rest need a design pass before they become
executable steps.

## 3. Reading quality, and the deferred performance items

Carried forward from the previous plan and the October audit: selection
granularity for shaped text, PDF text semantics for ligatures and BiDi,
accessibility of the custom-drawn reader, and the five performance items that
were explicitly deferred (PERF-01, 03, 04, 05, 06, 07).

**Steps:** Q-01 … Q-08 in [tasks.md](tasks.md#quality).

## Open decisions

These change user-visible output and should be settled before the matching
task is written:

- **Raw HTML.** Dropping it is defensible for a reader that has no HTML
  renderer; showing the tag text is worse than dropping it. Recommend: drop
  blocks, and render inline HTML's *content* while discarding the tags.
- **Front matter.** Recommend: recognise a leading `---` block or `+++` block,
  hide it from the reader, and keep the source editor unchanged.
- **Definition lists.** PHP Markdown Extra syntax; MD4C has no support, so this
  is a preprocessor in the footnote parser's style. Recommend: support it — it
  is common in technical Markdown.
- **Task-list interactivity.** Clicking a checkbox to edit the source is a nice
  reader affordance and a write path. Recommend: not yet.

## Not doing

- A streaming Markdown parser. The first screen may wait for the parse; it is
  the smaller floor.
- Replacing MD4C, HarfBuzz or the PDF writer.
- A Markdown-to-HTML renderer.
- Reflowing print view on window resize; that is the PDF-reader behaviour we
  keep.
