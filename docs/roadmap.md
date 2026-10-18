# Roadmap

Penelope reads a Markdown file the way an ebook reader reads a book: it typesets
what is on screen, keeps that work, and resumes. That pipeline is finished, and
so is the fidelity work that followed it. Every step in
[tasks.md](tasks.md) is implemented and covered by the regression suite
(114 slots), including the footnotes, the Markdown dialect gaps and the quality
items that used to be carried forward here.

What is left is listed below: one deferred piece of work, one deliberate
deferral per feature, and one project — rebasing onto `markoff-core`.

## Deferred, with reasons

| What | Why |
|---|---|
| Styled copy in the **PDF** reader | That mode has no display list to read, so it falls back to the block-based extraction. It disappears with the rebase. |
| **Definition lists** | Neither MD4C nor Markoff's parser recognises them; they are not part of the dialect we target. |
| **Task-list interactivity** | Clicking a checkbox edits the source: a write path a reader does not need. |
| A **streaming** Markdown parser; replacing MD4C, HarfBuzz or the PDF writer | The first screen may wait for the parse; the parse is the smaller floor at the sizes we measured. |
| Reflowing print view on window resize | That is the PDF-reader behaviour we keep. |

## The rebase onto markoff-core

The one substantial project left, unchanged from when it was first recorded.
What it costs, from the current Markoff tree:

- **`markoff-parser` first.** It is the standalone seam: `Markoff::Document`
  from `fromMarkdown()` is a value snapshot with frontmatter, footnotes, links,
  tags and per-block byte ranges. It needs libtree-sitter, its vendored grammar
  and rapidyaml, all linked privately into the parser library; it also needs
  Qt 6.8 and links Qt Gui and Widgets for its `QTextDocument` table handler.
  Adopting it replaces MD4C and changes coordinates: UTF-8 byte offsets,
  block-relative spans, per-block buffers whose conventions differ (headings
  include `#`, list markers are removed and rebuilt from attributes, code
  blocks include fences). Penelope's line-based source map and `ScrollAnchor`
  would be re-anchored to block ids and byte ranges.
- **`markoff-core` second, and it is not a library drop-in.** A document cannot
  be opened without a `DocumentStorageProvider`, which means the installed
  CollabText Core package; Markoff's CMake fails to configure without it. Core
  itself renders nothing: `MarkdownView` is an abstract contract base, and the
  leaves (canvas, styled, source) live outside it. Accessibility, image and math
  rendering, resource resolution (`Vault::ResourceProvider`), embeds
  (`EmbedRegistry`) and footnote reading are host- or leaf-supplied. Standalone
  Markoff installs no CMake package today.

**Prerequisites before a rebase is worth attempting:** an installed or exported
Markoff target; a read path that does not demand a durable provider; multi-line
note content plus a footnote read accessor in the parser and core; and a
projection layer from per-block buffers to Penelope's box tree.

## What the reader does today

For reference when judging a change, the parts that matter:

- **Pipeline.** Markdown → MD4C (GFM plus underline-free emphasis, wikilinks,
  LaTeX spans) with front-matter and footnote pre-passes → `Content::Document`
  value snapshot → layout cursor on a worker (fragment caches, cancellation) →
  box tree → painted pages or a continuous column → PDF, RTF and clipboard.
- **Footnotes.** Extracted, numbered by the layout (per document or per page),
  printed in a band at the foot of the page that cites them, with citations and
  markers navigating both ways.
- **Reading.** Six arrangements, zoom, hover hints for links and images, a
  heading tree, per-file view state, file watching with position-preserving
  reload, and an accessible per-block tree for assistive technology.
- **Math.** `Content::Math` carries the LaTeX source; the layout engine typesets
  it with the vendored jkqtmathtext (XITS and Fira Math compiled in) and puts the
  result in the flow as a box with the image's metrics. The source stays in the
  box's text, so copying and the PDF text layer keep the formula. The library
  needs Qt 6.8, which is now the project's floor.
