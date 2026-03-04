# Penelope: Project Planning Roadmap

## Locked-In Requirements

| Aspect | Decision |
|--------|----------|
| Primary purpose | Markdown reader -- beautiful paginated rendering, like a PDF |
| Editing | Deferred. Plain-text mode available, robust editing is last priority |
| Markdown support | Ambitious -- QOwnNotes-level. Footnotes, task lists, tables. Math deferred |
| Rendering | True WYSIWYG for common elements. No markup visible in primary view |
| Style system | Theme presets + granular per-element controls |
| Style storage | XDG config directory. Per-file metadata. Templates shareable |
| Print/PDF | Headers, footers, page numbers, TOC, custom margins, page size |
| Multi-document | Tabs |
| File navigation | Dockable file browser sidebar |
| KDE Frameworks | Embraced -- KF6 dependencies encouraged |
| Code highlighting | KSyntaxHighlighting (Kate engine, 300+ languages, theme support) |
| Images | Interactive controls -- resize handles, alignment, wrapping |
| Program metadata | Never touches the .md file. All settings/styles/overrides stored externally |

---

## Architectural Vision

### Rendering Pipeline

```
.md file on disk (never modified by Penelope)
    | read
    v
MD4C Parser (CommonMark + GitHub Flavored extensions)
    | SAX callbacks
    v
DocumentBuilder (custom)
  Populates QTextDocument from parsed markdown
  Maps markdown elements to named styles
  Inserts image shapes, code blocks
    |
    v
Style Engine (adapted from Calligra)
  KoStyleManager, KoParagraphStyle, KoCharacterStyle
  User themes + per-element overrides from XDG config
  KSyntaxHighlighting for code block theming
    |
    v
Layout Engine (adapted from Calligra)
  KoTextDocumentLayout -> KoTextLayoutArea
  Pagination, columns, widow/orphan
  Header/footer frames (separate QTextDocuments)
  Image shape positioning + text wrapping
    |
    v
Canvas Widget (custom, inspired by KWCanvas)
  QWidget rendering pages via QPainter
  Page-by-page scrollable view
  Shape selection/resize for images
  Zoom support
    |
    v
Print Engine (adapted from Calligra KoPrintingDialog)
  QPrinter -> PDF or physical printer
  Headers/footers with fields (page#, title, date)
  Page range, margins, orientation
```

### Module Boundaries

Six logical modules, each a CMake library target:

| Module | Responsibility | Key Sources |
|--------|---------------|-------------|
| PrettyCore | Document model, style engine, page management | Adapted from Calligra libs/text/styles/, libs/odf/KoPageLayout |
| PrettyMarkdown | MD4C integration, markdown-to-QTextDocument builder | MD4C library + custom builder |
| PrettyLayout | Paginated text layout, shape positioning | Adapted from Calligra libs/textlayout/ |
| PrettyCanvas | Rendering widget, shape interaction, zoom | Inspired by Calligra KWCanvas + libs/flake/ subset |
| PrettyPrint | Print/PDF pipeline, header/footer frames | Adapted from Calligra libs/main/KoPrintingDialog |
| PrettyApp | Main window, tabs, docks, file browser, settings | Follows refapp patterns (KXmlGuiWindow, KConfig) |

### Technology Selections

| Need | Solution | Source |
|------|----------|--------|
| Markdown parsing | MD4C | QOwnNotes src/libraries/md4c/ |
| Style management | KoStyleManager + KoParagraphStyle + KoCharacterStyle (adapted) | Calligra libs/text/styles/ |
| Text layout/pagination | KoTextDocumentLayout (adapted) | Calligra libs/textlayout/ |
| Page geometry | KoPageLayout + KoPageFormat | Calligra libs/odf/ |
| Image shapes | Simplified KoShape subset | Calligra libs/flake/ (heavily trimmed) |
| Code highlighting | KSyntaxHighlighting | KDE Framework (system library) |
| Code-to-HTML (export) | CodeToHtmlConverter patterns | QOwnNotes |
| Main window / XMLGUI | KXmlGuiWindow | KDE Framework |
| Config / settings | KConfig + kcfg schema | KDE Framework |
| Tab management | QTabWidget or KTabWidget | Qt/KDE |
| File browser | KDirModel + QTreeView or KFileWidget | KDE Framework |
| Sidebar/docks | QDockWidget | Qt (native) |

---

## Planning Stages

### Stage 1: Calligra Source Extraction Assessment

**Status: IN PROGRESS**

Goal: Determine exactly which Calligra files to extract and what dependencies they
carry that must be stubbed or replaced.

Activities:
- Inventory every file in libs/text/styles/ and map #include dependencies
- Inventory libs/textlayout/ and map dependencies on libs/flake/
- Determine minimum viable subset of libs/flake/ for image shapes
- Determine which Calligra deps we keep vs. unnecessary bloat
- Produce concrete file list with dependency graph

Decisions:
- How much of the flake shape system do we need?
- Do we keep Calligra's KoTextShapeData binding?
- Header/footer: follow Calligra's separate KWTextFrameSet pattern or simplify?

### Stage 2: Markdown-to-QTextDocument Mapping

Goal: Design the exact mapping from markdown elements to QTextDocument structures
and named styles.

Activities:
- Define named style set (Heading1-6, BodyText, BlockQuote, CodeBlock, etc.)
- Design QTextBlock/QTextFragment/QTextCharFormat mappings per element
- Design image insertion model (QTextImageFormat + parallel KoShape?)
- Design code block handling (inline styled regions vs. code shape/flake)
- Design footnote handling (bottom-of-page in paginated layout)

Decisions:
- Code blocks: inline styled text vs. code flake shapes?
- Tables: QTextTable vs. custom table shape?
- Task lists: interactive checkboxes in reader mode?

### Stage 3: Style System & Persistence Design

Goal: Design theme/style data model, dock panel UI, and XDG storage format.

Activities:
- Define style data model (JSON schema for themes, per-element properties)
- Design theme preset system (built-in, user-created, per-file overrides)
- Design dock panel layout (element style tree? tabbed sections? live preview?)
- Design XDG directory structure
- Design code highlight theme integration

Decisions:
- Per-file settings keyed by path or content hash?
- Can a theme include page layout or just typography/colors?
- Code color scheme: bundled with document theme or independent?

### Stage 4: Canvas & Interaction Design

Goal: Design the page-rendering canvas widget and image interaction model.

Activities:
- Design canvas widget (page rendering, scrolling, zoom)
- Design image interaction layer (selection handles, resize, alignment)
- Design page layout display (single page, spread, continuous scroll)
- Evaluate QGraphicsView/QGraphicsScene vs. raw QWidget+QPainter

Decisions:
- QGraphicsView vs raw QPainter canvas?
- View modes: continuous scroll vs page-at-a-time? Both?
- Zoom: fit-width, fit-page, custom percentage?

### Stage 5: Print & PDF Pipeline Design

Goal: Design print/PDF output including headers, footers, TOC.

Activities:
- Design header/footer content model (page number, total, title, date, custom)
- Design TOC generation (scan headings, generate page-numbered index)
- Design print dialog (extend KoPrintingDialog or write fresh?)
- Design PDF metadata

### Stage 6: Implementation Milestones

Proposed milestone sequence:

| # | Milestone | Deliverable |
|---|-----------|------------|
| M1 | Project skeleton | CMake build, empty KXmlGuiWindow, tab widget, one dock. Compiles and runs. |
| M2 | Markdown loading + basic rendering | MD4C integration, DocumentBuilder, QTextEdit (read-only) with formatted markdown |
| M3 | Style engine | KoStyleManager integration, named styles, theme loading from JSON |
| M4 | Style dock panel | Theme selector + per-element controls, live preview |
| M5 | Code block highlighting | KSyntaxHighlighting integration, theme selector in dock |
| M6 | Paginated canvas | Canvas widget with KoTextDocumentLayout, visible pages with margins |
| M7 | Print/PDF | Print pipeline, headers/footers with fields, PDF export |
| M8 | Images | Image rendering from markdown, interactive controls, per-file metadata |
| M9 | File browser + multi-document | File browser dock, tab management, recent files, session restore |
| M10 | Tables, footnotes, advanced elements | QTextTable, footnotes, task lists, horizontal rules |
| M11 | TOC + advanced print | Table of contents, advanced print options |
| M12 | Plain-text editing mode | Source view with MarkdownHighlighter, live re-render on switch |

### Stage 7: Risk Register

| Risk | Mitigation |
|------|-----------|
| Calligra text layout extraction harder than expected | Fallback: QTextDocument built-in layout, add pagination as custom QAbstractTextDocumentLayout |
| KoShape system too heavy for image interaction | Fallback: custom lightweight shape overlay on QGraphicsView |
| KSyntaxHighlighting doesn't integrate cleanly | Fallback: QSyntaxHighlighter subclass using KSyntaxHighlighting definitions |
| MD4C output doesn't map cleanly to QTextDocument | Fallback: cmark-gfm AST-based approach |
| Performance with large markdown files | Lazy page rendering, only layout visible pages |
