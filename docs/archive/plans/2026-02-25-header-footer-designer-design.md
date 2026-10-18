# Header & Footer Designer — Design Document

## Goal

Replace the awkward placeholder-tag text fields in the Page dock with a modern drag-and-drop tile designer in a dedicated modal dialog. Vet all header/footer drawing routines for correctness.

## Current State

The infrastructure is solid:
- `PageLayout` stores headerLeft/Center/Right and footerLeft/Center/Right strings
- `MasterPage` system supports first/left/right page overrides with inheritance
- 6 placeholders: `{page}`, `{pages}`, `{title}`, `{filename}`, `{date}`, `{date:format}`
- `HeaderFooterRenderer` draws headers/footers in canvas (PageItem) and print (PrintController)
- `PdfGenerator::renderHeaderFooter` re-implements drawing for PDF output
- Layout engine correctly subtracts header/footer heights from content area

The problem: the UI is 6 bare QLineEdit fields in the Page dock where users must type placeholder tags manually — no discoverability, easy to mistype.

## Design

### 1. Page Dock Simplification

Remove the 6 QLineEdit fields and placeholder hint from `PageLayoutWidget`. Replace with:
- **Header checkbox** (keep existing)
- **Footer checkbox** (keep existing)
- **"Edit Headers & Footers..." QPushButton** — enabled when either checkbox is checked, launches the modal dialog
- The page type dropdown, page size, orientation, and margins stay unchanged in the dock

### 2. HeaderFooterDialog (new modal)

A QDialog containing:

**Tile Palette** (top of dialog):
A horizontal flow of draggable tiles in a labeled group:
- **Snippet tiles:** "Page X of Y" → `{page} / {pages}`, "Page Number" → `{page}`, "Title" → `{title}`, "Filename" → `{filename}`, "Date" → `{date}`, "Full Date" → `{date:d MMMM yyyy}`
- Each tile is a small rounded-rect QLabel with drag support (QDrag with text/plain MIME)

**Header Section:**
A labeled group with three drop-target fields in a row: Left | Center | Right
- Each field is a QLineEdit subclass (`DropTargetLineEdit`) that:
  - Accepts drops (inserts placeholder text at cursor position)
  - Also accepts normal keyboard input for free text
  - Shows placeholder text like "Left", "Center", "Right"

**Footer Section:** Same three-field row layout as header.

**Page Variation Options:**
- "Different first page" checkbox — when checked, reveals a "First Page" section with its own header+footer rows. Fields show "(inherit)" as placeholder text when empty.
- "Different odd/even pages" checkbox — when checked, reveals "Left (Even)" and "Right (Odd)" sections each with their own header+footer rows, hiding the default header/footer sections. Fields show "(inherit)" as placeholder when empty.

**Dialog buttons:** OK and Cancel (QDialogButtonBox).

**Data flow:** On open, the dialog reads from the current `PageLayout` (including `masterPages`). On OK, it writes back to the `PageLayout` model and emits `pageLayoutChanged()`.

### 3. Drawing Routine Audit

Three rendering paths exist:
1. **Canvas (PageItem)** — uses `HeaderFooterRenderer::drawHeader/drawFooter` ✓
2. **Print (PrintController)** — uses `HeaderFooterRenderer::drawHeader/drawFooter` ✓
3. **PDF (PdfGenerator)** — re-implements text rendering manually using raw PDF operators

Audit tasks:
- Verify PdfGenerator header/footer output matches the QPainter-based rendering visually
- Check that `resolvedForPage()` is called correctly in all three paths
- Confirm separator lines render consistently
- Web/continuous view intentionally does NOT render headers/footers (by design)

### 4. New Classes

- `HeaderFooterDialog` — the modal dialog (QDialog subclass)
- `DropTargetLineEdit` — QLineEdit subclass that accepts drag-and-drop of tile text
- `DragTileWidget` — small QWidget for each draggable tile in the palette

### 5. Files Affected

- **New:** `src/widgets/headerfooterdialog.h/cpp`
- **New:** `src/widgets/droptargetlineedit.h/cpp`
- **Modify:** `src/widgets/pagelayoutwidget.h/cpp` — remove header/footer QLineEdits, add button
- **Modify:** `src/CMakeLists.txt` — add new source files
- **Audit:** `src/pdf/pdfgenerator.cpp` — verify `renderHeaderFooter`
- **Audit:** `src/canvas/pageitem.cpp` — verify header/footer rects
- **Audit:** `src/print/printcontroller.cpp` — verify header/footer rendering
