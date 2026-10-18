# PDF Export Options Dialog — Design

## Overview

A `KPageDialog`-based export options dialog shown before saving a PDF.
Three sidebar pages: General, Content, Output. Settings persist globally
via KConfig and per-document via MetadataStore.

## Dialog Class

`PdfExportDialog` subclasses `KPageDialog` with `FlatList` face type
(icon list on the left, content on the right — KDE System Settings
style). Accepts a `Content::Document` reference for building the heading
tree, and the current page count for page range validation.

Returns a `PdfExportOptions` struct on accept.

## Data Structure

```cpp
struct PdfExportOptions {
    // General — metadata
    QString title;
    QString author;
    QString subject;
    QString keywords;          // comma-separated

    // General — text copy behavior
    enum TextCopyMode { PlainText, MarkdownSource, UnwrappedParagraphs };
    TextCopyMode textCopyMode = PlainText;

    // Content — section selection
    QSet<int> excludedHeadingIndices;   // indices into doc.blocks of unchecked headings
    bool sectionsModified = false;      // true if user changed any checkboxes

    // Content — page range
    QString pageRangeExpr;              // raw expression, empty = all pages
    bool pageRangeModified = false;     // true if user entered a range

    // Output — bookmarks
    bool includeBookmarks = true;
    int bookmarkMaxDepth = 6;           // 1–6

    // Output — viewer preferences
    enum InitialView { ViewerDefault, ShowBookmarks, ShowThumbnails };
    InitialView initialView = ShowBookmarks;

    enum PageLayout { SinglePage, Continuous, FacingPages, FacingPagesFirstAlone };
    PageLayout pageLayout = Continuous;
};
```

## Page 1: General

### Metadata Group

| Field    | Widget     | Default            | Notes                    |
|----------|------------|--------------------|--------------------------|
| Title    | QLineEdit  | Document filename  | Pre-filled from tab name |
| Author   | QLineEdit  | Empty              | Free text                |
| Subject  | QLineEdit  | Empty              | Free text                |
| Keywords | QLineEdit  | Empty              | Comma-separated          |

### Text Copy Behavior Group

A `QComboBox` with three options:

- **Plain text** (default) — current behavior. Text copied from the PDF
  in a viewer reads as plain readable text. Line breaks match the PDF
  layout (wrapped lines have hard breaks).

- **Markdown source** — the ToUnicode CMap maps rendered glyphs back to
  the original markdown. Copying bold text returns `**bold**`, headings
  return `# Heading`, etc. Useful for technical documentation where
  readers may want to extract the source.

- **Unwrapped paragraphs** — strips soft line breaks within paragraphs
  so copying returns flowing text without mid-paragraph breaks. Paragraph
  boundaries are preserved as double newlines. Useful for readers who
  paste into editors that reflow text.

Implementation: the text copy mode affects how the ToUnicode CMap is
generated in PdfWriter. For "Markdown source", the content builder must
pass the original markdown fragments through the layout pipeline so they
are available at CMap generation time. For "Unwrapped paragraphs", the
CMap omits newline mappings at soft line break positions.

## Page 2: Content

### Section Selection (top area, takes most vertical space)

A `QTreeWidget` showing the document's heading hierarchy with
checkboxes, mirroring the TOC panel. Built from `Content::Document` by
scanning for `Content::Heading` blocks.

- All headings checked by default.
- Checking/unchecking a parent cascades to children.
- Unchecking a heading excludes it and all content up to the next
  heading of the same or higher level.
- A "Select All" / "Deselect All" button pair below the tree.

### Page Range (bottom area)

A `QLineEdit` with placeholder text: `e.g. 1-5, 8, first, (last-3)-last`

Supported syntax:
- Literal page numbers: `1`, `5`, `12`
- Ranges: `1-5`, `8-12`
- Keywords: `first` (alias for 1), `last` (alias for total page count)
- Arithmetic from `last`: `last-3`, `(last-3)-last`
- Comma-separated combinations: `1-5, 8, (last-3)-last`

Parsing produces a `QSet<int>` of 1-based page numbers. Invalid tokens
show inline validation feedback (red border + tooltip).

### Conflict Warning

When both `sectionsModified` and `pageRangeModified` are true, a
`KMessageWidget` (inline warning bar) appears between the two sections:

> "Both section selection and page range are active. Only pages that
> match **both** filters will be exported. This may produce unexpected
> results."

The warning is informational (`KMessageWidget::Warning` type) and
dismissable.

## Page 3: Output

### Bookmarks Group

| Field           | Widget              | Default | Notes             |
|-----------------|---------------------|---------|-------------------|
| Include         | QCheckBox           | Checked |                   |
| Maximum depth   | QSpinBox (1–6)      | 6       | Disabled if unchecked |

### Viewer Preferences Group

| Field        | Widget    | Default          | Options                                       |
|--------------|-----------|------------------|-----------------------------------------------|
| Initial view | QComboBox | Show bookmarks   | Viewer default, Show bookmarks, Show thumbnails |
| Page layout  | QComboBox | Continuous       | Single page, Continuous, Facing pages, Facing pages (first alone) |

## Settings Persistence

### Global Defaults (KConfig)

A new `[PdfExport]` group in the app's KConfig stores last-used values
for all fields except content selection (heading checkboxes and page
range are document-specific).

Persisted fields: `author`, `textCopyMode`, `includeBookmarks`,
`bookmarkMaxDepth`, `initialView`, `pageLayout`.

Not persisted globally: `title` (derived from filename), `subject`,
`keywords`, `excludedHeadingIndices`, `pageRangeExpr`.

### Per-Document Overrides (MetadataStore)

The existing `MetadataStore` (keyed by file path) stores a JSON object
under key `"pdfExportOptions"` containing any fields the user has
customized for that specific document. On dialog open:

1. Load KConfig global defaults.
2. Overlay per-file overrides from MetadataStore (if any).
3. Pre-fill title from document filename.

On dialog accept:

1. Save global-scoped fields to KConfig.
2. Save all fields (including document-specific ones like heading
   selection and page range) to MetadataStore for this file.

## Integration with Export Pipeline

`MainWindow::onFileExportPdf()` changes:

```
Before:  show file dialog → build content → layout → generate PDF
After:   build content → show PdfExportDialog → show file dialog
          → filter content by sections → layout → filter pages by range
          → generate PDF with options
```

The content must be built before the dialog opens so the heading tree
and page count are available. The section filter removes blocks from
`Content::Document` before layout. The page range filter removes pages
from `LayoutResult` after layout.

### PdfGenerator Changes

`PdfGenerator` receives `PdfExportOptions` (or relevant subset) to:

- Set metadata fields in the PDF Info dictionary.
- Control bookmark generation (include/exclude, depth filter).
- Set viewer preferences (`/ViewerPreferences` and `/PageMode` in
  catalog).
- Adjust ToUnicode CMap generation based on text copy mode.

### PdfWriter Changes

- Add `setViewerPreferences()` to write `/ViewerPreferences` dict.
- Add metadata setters for author, subject, keywords.
- Support page layout mode in catalog (`/PageLayout` key).
- For "Markdown source" text mode: accept alternative Unicode mappings
  per glyph run for CMap generation.

## Files Changed

| File | Change |
|------|--------|
| `src/widgets/pdfexportdialog.h/cpp` | New: KPageDialog subclass |
| `src/model/pdfexportoptions.h` | New: PdfExportOptions struct |
| `src/app/mainwindow.cpp` | Wire dialog into export flow |
| `src/pdf/pdfgenerator.h/cpp` | Accept options, metadata, bookmarks filter |
| `src/pdf/pdfwriter.h/cpp` | Viewer prefs, metadata fields, CMap modes |
| `src/app/penelope.kcfg` | Add PdfExport settings group |
| `src/CMakeLists.txt` | Add new source files |
