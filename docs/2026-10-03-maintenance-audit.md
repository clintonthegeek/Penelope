# Penelope maintenance audit

Date: 2026-10-03. This records the maintenance changes and their verification,
plus limitations found during the source audit. It is not a claim that every
possible input, platform or rendering defect has been eliminated.

## What changed

### Source, tabs and reloads

The in-memory source is now the authority for preview, printing and export.
Previously, leaving source mode or exporting could reread the disk and silently
lose or ignore edits. Source changes rebuild the active reader after a short
debounce; inactive tabs are marked stale. Save is available through the File
menu and Ctrl+S, using atomic replacement. Dirty tabs show an asterisk and
closing a tab/window offers Save, Discard and Cancel. Saving over a changed or
removed file requires an explicit overwrite decision.

The file watcher follows both the file and its directory, so editor-style
atomic replacements keep being observed. Reload preserves scroll position and
honors the auto-reload preference. External changes do not overwrite unsaved
source. The disk baseline is checked again when saving, including changes
whose watcher notification has not yet been delivered.

Closing a tab now destroys its view and render worker instead of merely
removing it from the tab bar. Canonical paths prevent duplicate tabs opened
through symlinks. Session restoration follows file identities when some saved
files have disappeared. Saved sidebar collapse, per-file view state, and
default page-size/zoom settings are applied. View actions and status updates
follow the active tab; inactive view signals do not change its controls.

### Reader and clipboard

Switching between continuous, PDF and legacy readers clears the appropriate
scene pointers, selection, PDF document, cache and link state. This removes a
dangling web-item pointer and old PDF ownership that could survive a mode
transition. Restoring a page in single/facing-page modes also updates the
visible page arrangement.

Continuous-reader selection now paints its highlight and supports Markdown
and RTF copy. Plain-text copy converts selected Markdown to text rather than
including source markers; the Markdown clipboard flavor keeps the source.
Ctrl+C in source mode copies the editor selection. Quotes carry source ranges
for styled selection, and language overrides traverse code blocks in nested
lists and quotes as well as top-level blocks.

The PDF cache distinguishes device-pixel ratios. Requests carry the document
generation when queued; stale requests and completions are rejected. Poppler
page destruction happens while the document lock is held, before the owning
document can be detached or deleted.

### Parsing and layout

Footnote definitions and continuation lines are extracted consistently.
Blanking their lines preserves source-map coordinates. Ordinary following
paragraphs are retained; fenced/indented code and inline-code reference
examples are not treated as notes. Numeric entities retain supplementary
Unicode characters instead of truncating them to one UTF-16 unit.

Nested lists and quotes now preserve container order. Tight-list paragraphs
close before nested blocks, with source coordinates retained. Lists lay out
code, headings, quotes, tables, rules and note sections rather than dropping
non-paragraph children.

Tables larger than an empty page enter the splitting path, retain their source
map and horizontal position, and repeat header rows. The paginator checks
whether a header and first body row fit in the remaining space. Paragraph
splitting leaves the configured minimum number of lines on the continuation
instead of needlessly rejecting a nearly fitting first fragment.

Images embedded in paragraphs are emitted into the flow in both paginated and
continuous layout, including paragraphs in lists/quotes. Missing assets show
alternative text. Image hiding honors its preference. Alpha is preserved from
the decoded image through PDF soft-mask embedding. Landscape page geometry is
used consistently by layout and the PDF MediaBox.

Notes now show their numbers, and separator length/stroke width use the point
units shown in the settings. Unsupported page-bottom placement and per-page
renumbering controls are disabled with explanatory tooltips; the interface
reflects the actual endnote behavior.

### Export, preferences and resources

PDF export and printing consume current source from the continuous reader as
well as print/source views. RTF export uses the semantic model in the modern
pipeline. Legacy printing/export clones the QTextDocument instead of mutating
the reader's layout. PDF headers/footers receive filename/title metadata.
PDF and semantic RTF output use checked atomic writes.

Page-range parsing rejects empty items and overflowing numbers; standalone
`last-N` means arithmetic, and a document with no pages is invalid. The export
dialog disables acceptance for invalid ranges.

Both Markdown pipelines honor the syntax-highlighting toggle and selected
highlight theme. Character-style underline and strike-through controls now
apply explicit style properties. Per-file persistence honors its enable flag
and expiry preference (zero days means no expiry).

Resource writes are atomic and checked, generated IDs avoid built-in
collisions, assigned IDs are written into JSON, unsafe path IDs are rejected,
and a failed file deletion leaves the registry entry intact. The optional
local-socket single-instance receiver waits for a complete serialized message
instead of handling partial socket input as an empty activation.

## Verification

CTest now runs a QtTest regression executable with isolated configuration/data
directories and an offscreen Qt platform. Release CI runs CTest before packaging.
The suite covers page ranges, footnote source preservation, Unicode entities,
widows, large-table continuation, nested block structure, source saving and
atomic reloads, cache density/invalidation, tab/view lifetime, continuous
selection and clipboard flavors, landscape PDF metadata/text, image alpha,
highlighting/image preferences, metadata expiry, resource integrity,
character decorations, and legacy-print layout preservation.

Generated PDFs are opened with Poppler and checked for page size and extracted
text; the image test checks rendered pixels as well as the soft-mask entry.
These tests exercise outputs, not just the presence of layout objects.

Local validation commands:

```sh
cmake -S . -B build
cmake --build build -j 4
ctest --test-dir build --output-on-failure
cmake -S . -B /tmp/penelope-fallback-build \
  -DCMAKE_DISABLE_FIND_PACKAGE_KF6DBusAddons=ON -DBUILD_TESTING=OFF
cmake --build /tmp/penelope-fallback-build -j 2
```

Final local results: both application builds succeeded; both CTest entries
passed. QtTest reported 32 passing cases (including initialization/cleanup),
with no failures or skips. `git diff --check` passed, and documentation links
were checked against the local files. Markoff's own tests were not run because
that checkout was inspected without changes.

Physical printer output, interactive platform dialogs, all six arrangements
across real monitors, screen-reader behavior and a complete Unicode/BiDi
conformance corpus are not certified by these checks.

## Remaining capabilities and risks

These require further design or broader renderer work; the existing controls
and documentation must not imply that they already work completely.

| Area | Current limit | Follow-up |
|---|---|---|
| Footnotes | Definitions form an endnote section. Page-bottom placement and per-page numbering are unavailable; note bodies do not retain full Markdown structure. | Paginate references and notes together; project rich note contents. |
| Master-page margins | First/odd/even headers and footers can vary, but body layout uses base page geometry. | Page-specific width/height constraints and reflow. |
| Oversize blocks | Table splitting occurs between rows; a row taller than a page can overflow. Large indivisible content still needs a clear policy. | Split cell contents or provide documented scale/overflow handling. |
| Inline media | Paragraph images flow as separate blocks; images in heading/table inline collections are not fully projected. | Define inline-versus-block media semantics and a shared asset contract. |
| Markdown dialect | Math rendering, raw HTML, Markoff embeds/frontmatter and structured wiki-link semantics are not a complete shared dialect. | Explicit projection/fallback rules; do not assume a parser swap supplies rendering. |
| PDF text semantics | Complex shaping, ligatures and BiDi need broader extraction/selection fixtures. | Cluster-aware text mappings, multilingual golden fixtures. |
| Selection | Source-region selection is coarse and styled copy can include an entire overlapping semantic block. Word selection is PDF-specific. | Character/cluster anchors and consistent continuous-view word selection. |
| Accessibility | The custom graphics reader lacks the mature per-block accessibility work present in Markoff canvas. | Expose read-only document/text navigation and test with assistive technology. |
| Responsiveness | Parsing, shaping, layout and PDF generation run synchronously; only PDF rasterization is asynchronous. | Immutable rendering snapshots and cancellable worker jobs. |
| Single-instance startup | The fallback receiver handles partial messages, but simultaneous startup/socket acquisition and malformed peer input need further coverage. | Ownership-aware startup and bounded framing. |
| Shared code | Legacy and modern pipelines duplicate preprocessing and export behavior. | Extract the modern engine with explicit options and ownership; retire legacy only after parity checks. |

The [Markoff analysis](2026-10-03-markoff-port-analysis.md) separates these
quality requirements from the integration work and proposes a staged extraction.
