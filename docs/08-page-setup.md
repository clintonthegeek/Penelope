# Page setup

How a document's pages are described, and what each part of the description
reaches. Everything here is `PageLayout` (`src/style/pagelayout.h`), which
travels with a page template, is edited by the Page panel and the header/footer
editor, and is read by the layout engine, the reader and the PDF writer.

## The model

| Field | Meaning |
|---|---|
| `pageSizeId`, `customSizeMm` | A standard size, or the page's own width and height in millimetres when the size is `Custom` |
| `orientation` | Portrait or landscape |
| `margins` | Left, top, right, bottom in millimetres. Zero is allowed |
| `headerEnabled`, `footerEnabled` | Whether the band is drawn |
| `headerLeft/Center/Right`, `footerLeft/Center/Right` | The six fields, with tokens |
| `chromeFontSize`, `chromeColor`, `chromeBold`, `chromeItalic` | The chrome's own typography, independent of the content styles |
| `masterPages` | Overrides for the `first`, `left` and `right` page types |
| `pageBackground` | Owned by the palette, not by the template |

A `MasterPage` carries the same header/footer fields plus `headerEnabled`,
`footerEnabled` and the four margins, each with an "unset" value (`-1`, or an
empty string) that means *inherit from the base layout*. `PageLayout::resolvedForPage(pageNumber)`
applies them: page 1 (and any chapter opener) is `first`, even pages are
`right`, odd pages are `left`, in the 0-based numbering the layout uses.

Sizes are stored as stable tokens (`A4`, `Letter`, `Tabloid`, `Custom`, …) so a
template written by an older build keeps its page, and a custom size is written
as `pageSizeCustom: {width, height, unit: mm}`.

## Tokens

`{page}`, `{pages}`, `{filename}`, `{title}`, `{date}`, and `{date:<Qt format>}`
— every occurrence of the latter is replaced. An unknown token is left as it was
written, so a typo shows up on the page rather than vanishing.

## Where each part lands

```
PageLayout ─┬─ resolvedForPage(pageNumber) ─┬─ Engine: content size for that page
            │                              ├─ BoxPageItem: content origin, chrome
            │                              ├─ HeaderFooterPlan: bands, rules, colour
            │                              └─ PdfGenerator: content area, chrome, rules
            └─ base layout ──────────────── page size, background, template identity
```

Each `Layout::Page` records the resolved layout it was laid out with
(`Page::layout`), and the reader and the PDF read that rather than resolving
again — so a page whose geometry had to be deferred (see below) still places its
content, its chrome and its rules consistently.

The header and footer bands come from `makeHeaderFooterPlan`, which both the
reader and the PDF writer use, so the screen and the printed page agree: the
same band rectangles, the same rules, the same colour, weight and slant, and a
centred field centred on the text column rather than on the paper.

## Page numbers

Page numbers exist as `{page}`/`{pages}` in the chrome. The paginated reader and
the PDF know their totals directly. The continuous reader has no pages of its
own, so the status bar's page indicator comes from a background pagination of
the current document (`MainWindow::startPageOracle`): one paginated layout on
its own worker, without the PDF, debounced, cancelled when the document changes
and cached per document revision and page geometry. It runs only when a header
or footer actually asks for page numbers. The visible source line, taken from
the viewport anchor, is mapped to a page through that layout's source map.

## A page type that changes the column width

Vertical margin overrides only change how much fits on a page, which the engine
handles without re-shaping anything. A horizontal override that changes the
*column width* has to re-break the block it interrupts:

1. The page's geometry is resolved as usual.
2. If the width differs from the page before, the queue of already-shaped
   elements is dropped, because it carries line breaks from the old width.
3. A block in progress is shaped again for the new width, and the lines whose
   characters are already on earlier pages are dropped. Characters, not lines,
   are the unit: a line that straddles the boundary is kept whole, so a re-break
   never loses text.
4. A table or a footnote band in progress keeps the geometry it started with,
   because its placed part cannot be shaped again. The next page break applies
   the override.

Mirrored margins — a wider inner margin and a narrower outer one — leave the
column width alone, so they change only where the column sits and never disturb
the line breaking.

## Tests

| Behaviour | Slot |
|---|---|
| Sizes survive a save, including a custom size | `pageSizesSurviveASave` |
| The panel drives size, custom size and units | `pageSizeWidgetDrivesTheLayout`, `pageUnitsConvertAndKeepMillimetres` |
| A page type's margins move the page, mirrored margins do not re-break it | `masterPageMarginsMoveThePage` |
| The editor keeps per-page settings and inserts tokens | `pageDialogKeepsPerPageOverrides`, `headerDialogEditsChromeAndTokens` |
| Chrome typography, rules and colour reach both renderers | `chromeTypographyReachesBothRenderers` |
| The page indicator follows the reader and the print view | `pageIndicatorShowsThePrintedPage`, `pageIndicatorFollowsThePrintView` |
| Builtin templates say they cannot be saved | `pageDockExplainsBuiltinTemplates` |
