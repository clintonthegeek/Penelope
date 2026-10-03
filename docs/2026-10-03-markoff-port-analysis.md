# Penelope → Markoff: reader and PDF rendering analysis

Date: 2026-10-03. Inspected the local Markoff checkout at `28e9a734` and
Penelope's source, with the maintenance changes described in
[the audit](2026-10-03-maintenance-audit.md). Markoff was inspected read-only;
no integration or modification of that repository was performed.

## Recommendation

Keep Penelope working independently. Extract its typesetting and PDF pipeline
into an optional library, then add a read-only Markoff view consuming that
library. That delivers the interesting upside: paginated reading, print
preview, and PDF export for applications already using Markoff.

Replacing Penelope's MD4C parser is a separate, lower-priority decision.
Penelope's parser is adequate for ordinary Markdown. Its most consequential
problems are state management and incomplete layout/rendering paths, which a
parser replacement would leave behind. A full Markoff adoption would be
justified if Penelope becomes an editor with the same document model as your
other applications, or if parity for Markoff-specific syntax becomes a product
requirement. Neither is necessary to share PDF rendering.

The reusable part is larger than a PDF writer but smaller than the whole app:
content projection, styles, font resolution/shaping, line breaking, pagination,
box rendering, and export. Penelope's KDE window, sidebars, file browser,
sessions, preferences, and file watching should remain host concerns.

## What the checkouts actually contain

Penelope's main pipeline is:

```text
Markdown → MD4C/ContentBuilder → Content::Document
         → Layout::Engine → boxes + pages + source map
         → PdfGenerator → PDF → Poppler/RenderCache → DocumentView
```

Its web view uses the same content/layout model with continuous layout and a
QPainter backend. There is also a legacy QTextDocument pipeline. All of these
currently live in `PenelopeCore`, together with the main window and KDE UI.
That target is an application implementation library, not an appropriate
public dependency for Markoff.

Markoff's current direction is its QWidget canvas leaf, not its former QML
live-preview leaf. `MARKOFF_BUILD_LIVE` defaults OFF in the actual
[CMake file](../../Markoff/CMakeLists.txt); the live leaf is retired. The
[status board](../../Markoff/docs/STATUS.md) says canvas production and the
accessibility arc are closed; styled is bug-fix-only and source is frozen.
The README's three-view account is historical. The status board also contains
older, inconsistent test totals below its latest entry; this analysis does
not treat them as a freshly verified test run.

The relevant integration seam is
[`MarkdownView`](../../Markoff/libs/markoff-core/include/markoff/core/MarkdownView.h).
[`Canvas::EditorWidget`](../../Markoff/libs/markoff-canvas/include/markoff/canvas/EditorWidget.h)
shows how an interchangeable leaf wraps a concrete view.
[`MarkoffDocument`](../../Markoff/libs/markoff-core/include/markoff/core/MarkoffDocument.h)
provides `serializeForSave()`, `blockText()`, `blockKind()`, `blockAttrs()`,
`inlineSpansFor()`, and document/block-change signals. Its model is the
content authority. Deprecated `toMarkdown()` and `toMarkdownUtf8()` read a
legacy buffer and must not be used to feed the reader.

## Comparing the possible integrations

| Direction | What it buys | Cost / tradeoff | Judgment |
|---|---|---|---|
| Leave Penelope on MD4C; repair it | Better reader immediately | Markdown dialect remains separate | Do now; this maintenance pass follows that route |
| Use `Markoff::Parser` in Penelope | Tree-sitter/frontmatter/structured link APIs | New AST adapter; does not fix pagination, PDF, or ownership | Consider only with a concrete dialect-parity requirement |
| Use `Markoff::Core` in Penelope | Shared model, commands, editor views, collaboration foundation | CRDT/session dependencies and document-authority migration for a mostly read-only app | Useful if Penelope evolves into an editor |
| Add paginated reader/PDF facilities to Markoff | Shared export and reading for existing Markoff consumers | Library extraction plus model/view adapters | Best strategic payoff |
| Add pagination to the existing editing canvas | Editing directly on printed pages | Couples editing virtualization/caret contracts to print pagination | Defer; read-only projection is much smaller |

The initial reader adapter may serialize a Markoff snapshot and feed MD4C.
That is an intentional compatibility bridge, not a replacement for the
Markoff model. It is suitable for explicit preview/export and debounced reader
refresh. It should never run a whole-document serialize/parse/layout on each
keystroke in an editing leaf.

## Proposed library boundaries

These names are proposals, not existing targets.

| Proposed target | Responsibility | Dependencies |
|---|---|---|
| `Markoff::Typeset` | Immutable content/style data, font ownership, shaping, line breaking, pagination and source maps | Qt Core/Gui, FreeType, HarfBuzz, ICU, fontconfig; optional highlighting/hyphenation |
| `Markoff::Pdf` | PDF generation, font subsets, images, links, bookmarks, metadata | Typeset, HarfBuzz-subset, zlib |
| `Markoff::Reader` | Read-only continuous/paginated QWidget, selection, navigation, render cache | Core, Typeset, Widgets; optional Poppler for exact PDF preview |
| Penelope application | KDE actions/docks, resource management, file/session policy, preferences, print dialog | The shared libraries plus existing KDE dependencies |

PDF export should not require Poppler. Poppler is the preview/printing backend;
keeping it outside the writer makes headless export easier to consume.
Continuous QWidget rendering could remain available without PDF preview.
KDE SyntaxHighlighting is already used by Markoff Core, but the portable
writer should consume highlighted spans or an injected highlighter rather
than require Penelope's settings singleton.

Qt minimums differ: Penelope declares 6.6; current Markoff targets declare
6.8. A Markoff leaf must use Markoff's supported baseline. Keep the new targets
optional so consumers uninterested in print do not acquire the entire
FreeType/Poppler/fontconfig toolchain. Markoff currently supports source-tree
consumption; do not assume an installed SDK/exported CMake package exists.

## Model projection and coordinate contracts

Start with a snapshot adapter. A print job receives document content, an
explicit resource resolver/base URI, resolved styles, page geometry, and
export options. It should not access live editors, global Penelope settings,
file-browser state, or host dialogs while rendering.

For the first bridge:

1. Snapshot `serializeForSave()` on the model's owning thread.
2. Preserve/handle frontmatter deliberately. Passing YAML frontmatter straight
   to MD4C would turn metadata into visible rules/headings.
3. Parse the snapshot once, resolve resources and styles, then layout/export.
4. Record the document revision and discard stale preview completions.

For the durable bridge, project the per-block model and cached inline spans
to the typesetting IR. This removes the extra parser and supports targeted
projection invalidation, but it needs explicit block-specific handling.
Markoff's parser
[`TopLevelBlock`](../../Markoff/libs/markoff-parser/include/markoff/parser/Document.h)
is a useful snapshot interface, not a complete nested print tree by itself.

| Markoff content | Projection requirement |
|---|---|
| Paragraph/heading | Preserve inline spans and style; distinguish ATX vs setext buffer conventions |
| List item | Reconstruct nesting, marker/start number, loose/tight grouping, task state from attrs; buffers omit markers |
| Block quote | Reconstruct nesting and quote markers; do not assume its buffer convention matches a paragraph |
| Code block | Separate fences/info string from body; preserve whitespace; apply highlighting spans |
| Table | Project cells, alignments, borders, repeated header semantics and row heights |
| Image/embed | Resolve through the host resource seam; carry size/alt text and alpha |
| Footnotes | Join definitions and references with stable IDs; decide endnotes vs page notes |
| Math/Mermaid/HTML | Define rendered asset/text fallback policy explicitly; MD4C syntax acceptance is not rendering support |
| Frontmatter/link definitions | Carry metadata/resolution context; usually omit from visible body |

Markoff uses UTF-8 byte offsets within blocks, plus separate global/view
coordinate spaces. Penelope's source map is currently line-based. A new leaf
should carry `BlockId` and a block-relative byte range through projection,
line boxes, page fragments and hit testing. Do not persist serialized line
numbers as stable Markoff anchors. Every split table or paragraph fragment
must retain its origin. Unicode tests must include astral characters and
combining marks, not just ASCII.

The reader remains read-only (`hasEditing() == false`). Selection and scrolling
are view state; the model owns content. Implement `setDocument(nullptr)` and
model destruction cleanly. Wire only the attached document's notifications.
Implement `fontScale`, themes, copy flavors and ephemeral scroll state.
Decide whether a navigation cursor is exposed; do not advertise editing/caret
support just to satisfy an assumed interface shape. Apply the current
[view guide](../../Markoff/docs/VIEW-IMPLEMENTORS-GUIDE.md) and
[invariants](../../Markoff/docs/INVARIANTS.md) before implementation.

## Rendering ownership and quality gates

Layout boxes contain borrowed `FontFace*` pointers. A reusable API cannot let
those pointers outlive their FontManager. Package the font arena, resource
assets and layout result in an owned render snapshot; retain it for the whole
preview/export lifetime. An asynchronously submitted PDF preview needs its
own immutable job inputs and generation, not shared mutable font-usage state
from another tab.

The maintenance audit exposed defects in tables, list children, view
transitions, preferences and PDF metadata. The new regression suite is a
starting point, not a typesetting conformance certification. Before shipping
a Markoff leaf, establish these gates:

- Equivalent print snapshots from standalone Penelope and the Markoff bridge
  for the shared Markdown subset; compare pages, source anchors and extracted
  text, with tolerance for platform font differences.
- Multi-page paragraphs/code/tables, nested lists/quotes, headings across page
  breaks, portrait/landscape and page-specific geometry.
- PNG transparency, missing assets, invalid resource URLs, large images, math
  and embed fallback behavior.
- Unicode/BiDi/ligatures and reliable PDF copy text; glyph-to-character lookup
  alone is insufficient for ligatures or multi-character clusters.
- Reader detach/reattach/destruction while rendering, out-of-order completions,
  monitor DPR changes, bounded memory and repeated open/close.
- A read-only view contract suite and accessible document/text navigation.
  Penelope's QGraphicsView reader does not automatically inherit canvas's
  newly developed per-block accessibility implementation.
- Markoff's full required checks and targeted consumer integration tests;
  no changes to the existing canvas/source/styled editing authority.

## Delivery sequence

1. **Stabilize Penelope.** Fix supported paths, replace untested claims with
   a regression suite and an explicit remaining-gap list. Done in part by this
   maintenance change; larger layout capabilities remain recorded in the audit.
2. **Extract without changing behavior.** Split application/UI from the engine;
   replace settings singleton reads with passed options; establish ownership
   and a small headless PDF fixture runner. Penelope continues consuming it.
3. **Build one Markoff consumer spike.** An optional reader wrapper and an
   explicit Export PDF action, driven by `serializeForSave()` snapshots. Gate
   it on shared-fixture parity and detach/reload tests.
4. **Replace the snapshot bridge where justified.** Add the direct per-block
   projection, stable block anchors, host media/link seams and targeted cache
   invalidation. Keep explicit export as a whole-document job.
5. **Adopt in an actual host.** Choose one existing Markoff consumer, make
   reading/export useful there, then decide whether Penelope should also adopt
   Markoff Core. Avoid moving every KDE preference dock into a widget library.

The uncertainty is concentrated in projection fidelity and print quality,
not widget embedding. A small read-only prototype can answer those questions
without committing Penelope or Markoff's editor to a broad architectural
rewrite. I would start there once the extracted pipeline passes the shared
fixtures.

## Licensing

Both projects carry GPL-family licenses, so this is not an obvious
incompatibility. Preserve existing Scribus-derived and other third-party
notices, bundled font/dictionary licenses, and individual SPDX tags during
extraction. Audit the exact files moved and dependencies distributed; the
repository license files alone do not settle every component's obligations.
