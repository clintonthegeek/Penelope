# Penelope: Calligra Source Extraction Analysis (Planning Stage 1)

## Executive Summary

Three parallel dependency analyses were conducted on Calligra's `libs/text/styles/`,
`libs/textlayout/`, and `libs/flake/`. The key findings are:

1. **The style system** is highly valuable but heavily coupled to ODF
   loading/saving infrastructure. Since we load from markdown (not ODF), all ODF
   code can be stripped, leaving a clean property-based style system over
   QTextCharFormat/QTextBlockFormat.

2. **The layout engine** has a critical abstraction point:
   `KoTextLayoutRootAreaProvider`. The core layout logic (pagination, line breaking,
   tables, footnotes) is shape-agnostic. Shape dependency is concentrated in the
   obstruction/anchor subsystem (text wrapping around objects).

3. **The flake shape system** is ~250 headers / 200+ cpp files. A minimum
   extraction for image support requires 35-45 files. However, a **better
   approach** is to use Qt's QGraphicsView for the canvas and interaction layer,
   avoiding flake extraction entirely.

### Recommended Strategy

| Subsystem | Approach | Rationale |
|-----------|----------|-----------|
| Style system | **Extract & adapt** (~20 files) | Too valuable to rewrite; strip ODF, keep property engine |
| Layout engine | **Extract & adapt** (~25 files) | Pagination/footnotes too complex to rewrite; implement own provider |
| Shape/canvas | **QGraphicsView + custom items** | Lighter than flake extraction; Qt handles zoom, selection, hit testing |
| Image interaction | **Custom QGraphicsItems** | ~800-1200 lines vs. 35-45 extracted flake files |

---

## I. Style System Dependencies

### Source: `libs/text/styles/` (11 classes, ~22 files)

#### Classes and Their Roles

| Class | Inherits | Role |
|-------|----------|------|
| KoCharacterStyle | QObject | ~90+ char properties (font, color, weight, etc.) |
| KoParagraphStyle | KoCharacterStyle | Spacing, indents, borders, lists, alignment |
| KoListStyle | QObject | Bullet/numbering per level |
| KoListLevelProperties | QObject | Per-level list formatting |
| KoTableStyle | QObject | Table-level formatting |
| KoTableCellStyle | QObject | Cell borders, padding, backgrounds |
| KoTableColumnStyle | (value type) | Column width properties |
| KoTableRowStyle | (value type) | Row height properties |
| KoSectionStyle | QObject | Section columns and formatting |
| KoStyleManager | QObject | Central registry for all named styles |
| Styles_p | (private) | Shared QMap<int,QVariant> property storage |

#### Dependency Categories

**Qt (keep as-is):**
QObject, QTextCharFormat, QTextBlockFormat, QTextTableFormat,
QTextTableCellFormat, QTextFrameFormat, QTextDocument, QTextBlock,
QTextCursor, QTextTable, QFont, QPen, QBrush, QColor, QVariant, QBuffer

**KDE Frameworks (keep -- we embrace KDE):**
KLocalizedString

**Calligra ODF infrastructure (STRIP -- we don't load ODF):**
- KoGenStyle, KoGenStyles -- ODF style generation
- KoStyleStack -- ODF style stack during loading
- KoOdfLoadingContext, KoOdfStylesReader -- ODF loading context
- KoShapeLoadingContext, KoShapeSavingContext -- shape load/save
- KoXmlReader, KoXmlNS, KoXmlWriter -- XML parsing
- KoStore, KoStoreDevice -- ZIP container access
- KoOdfNumberDefinition -- ODF number formats
- KoOdfBibliographyConfiguration, KoOdfNotesConfiguration
- KoOdfGraphicStyles
- KoTextLoader, KoTextWriter -- ODF text serialization

**Calligra internal utilities (EXTRACT alongside styles):**
- KoShadowStyle -- simple data holder for shadows
- KoBorder -- border definitions
- KoColumns -- column layout struct
- KoUnit -- measurement unit conversions
- KoText -- enums and Tab struct
- KoTextDocument -- QTextDocument wrapper
- KoTextBlockData -- per-block layout data

**External libraries:**
- fontconfig, freetype -- font enumeration (keep, available on system)
- kundo2stack.h -- undo framework (keep, or replace with QUndoStack)

#### Extraction Plan for Styles

**Files to extract (~20 files):**

From `libs/text/styles/`:
```
KoCharacterStyle.h/.cpp
KoParagraphStyle.h/.cpp
KoListStyle.h/.cpp
KoListLevelProperties.h/.cpp
KoTableStyle.h/.cpp
KoTableCellStyle.h/.cpp
KoTableColumnStyle.h/.cpp
KoTableRowStyle.h/.cpp
KoSectionStyle.h/.cpp
KoStyleManager.h/.cpp
Styles_p.h/.cpp
```

From `libs/text/`:
```
KoTextDocument.h/.cpp
KoText.h/.cpp
KoTextBlockData.h/.cpp
```

From `libs/odf/` (utility structs only):
```
KoPageLayout.h/.cpp
KoPageFormat.h/.cpp
KoUnit.h/.cpp
```

Co-extract as simple data holders:
```
KoShadowStyle.h/.cpp
KoBorder.h/.cpp
KoColumns.h/.cpp
```

**Modifications required:**
1. Remove all `loadOdf()` / `saveOdf()` methods from every style class
2. Remove all KoGenStyle, KoStyleStack, KoXmlReader includes and code
3. Remove KoShapeLoadingContext/KoShapeSavingContext dependencies
4. Remove KoOdf* dependencies
5. Add JSON serialization methods: `saveToJson()` / `loadFromJson()`
6. Remove KoImageData dependency from KoListLevelProperties (list bullet images
   can use QImage directly)
7. Replace kundo2stack with QUndoStack (or keep if available as KDE dep)
8. Simplify KoTextDocument to remove change tracker, inline object manager
   (not needed for reader mode)

**Estimated extraction effort: 2-3 days** (strip ODF, fix includes, add JSON, test)

---

## II. Text Layout Engine Dependencies

### Source: `libs/textlayout/` (35 files total)

#### Architecture

```
QAbstractTextDocumentLayout
    |
    v
KoTextDocumentLayout (THE orchestrator)
    |
    +--> KoTextLayoutRootAreaProvider (ABSTRACT -- key decoupling point)
    |        Provides rectangular regions for text layout
    |        Returns obstruction list for text wrapping
    |
    +--> KoTextLayoutRootArea (one per page/column region)
    |        |
    |        v
    |    KoTextLayoutArea (bounded text layout within rectangle)
    |        |
    |        +--> KoTextLayoutTableArea (table cells)
    |        +--> KoTextLayoutNoteArea (footnotes)
    |        +--> KoTextLayoutEndNotesArea (endnotes)
    |
    +--> RunAroundHelper (text wrapping around obstructions)
    +--> KoTextLayoutObstruction (shapes text wraps around)
    +--> AnchorStrategy (positions anchored objects)
         +--> FloatingAnchorStrategy
         +--> InlineAnchorStrategy
```

#### The Critical Abstraction: KoTextLayoutRootAreaProvider

This abstract interface is the **single point of decoupling** between the layout
engine and the shape/canvas system. It defines:

```cpp
class KoTextLayoutRootAreaProvider {
    virtual KoTextLayoutRootArea *provide(KoTextDocumentLayout *) = 0;
    virtual void releaseAllAfter(KoTextLayoutRootArea *) = 0;
    virtual void doPostLayout(KoTextLayoutRootArea *, bool isNewArea) = 0;
    virtual void updateAll() = 0;
    virtual QRectF suggestRect(KoTextLayoutRootArea *) = 0;
    virtual QList<KoTextLayoutObstruction*> relevantObstructions(...) = 0;
};
```

**For Penelope, we implement our own provider** that:
- Creates root areas corresponding to page regions
- Returns page rectangles (based on page layout settings)
- Returns obstructions for any images on the page
- Triggers canvas redraw in doPostLayout()

This means the layout engine works without modification -- we just supply our own
provider implementation.

#### Dependency on libs/flake (Shape System)

**Deeply intertwined (concentrated in 5 files):**

| File | Flake Dependency | Needed for |
|------|-----------------|------------|
| KoTextLayoutObstruction | KoShape, KoShapeGroup, KoClipPath, KoShapeShadow, KoShapeStrokeModel | Text wrapping around objects |
| AnchorStrategy | KoShapeAnchor::PlacementStrategy, KoShapeContainer | Positioning anchored objects |
| FloatingAnchorStrategy | KoShapeContainer | Floating object placement |
| InlineAnchorStrategy | KoShapeContainer | Inline object placement |
| KoTextShapeContainerModel | KoShapeContainerModel | Shape-text binding |

**Easily decoupled (uses shapes only through provider):**

| File | Shape Usage | Notes |
|------|------------|-------|
| KoTextDocumentLayout | Through provider only | Core logic is shape-agnostic |
| KoTextLayoutArea | No direct shape use | Pure text layout |
| KoTextLayoutRootArea | `KoShape* associatedShape` | Can be any shape or null |
| KoTextLayoutTableArea | None | Pure table layout |
| KoTextLayoutNoteArea | None | Pure footnote layout |

#### Dependency on libs/text (Style System)

The layout engine depends heavily on the style system (which we're extracting):
KoStyleManager, KoParagraphStyle, KoCharacterStyle, KoListStyle, KoTableStyle,
KoTableCellStyle, KoTableRowStyle, KoTableColumnStyle, KoTextDocument,
KoTextBlockData, KoBorder, KoUnit, KoText

This is expected and fine -- we extract both together.

#### Extraction Plan for Layout Engine

**Files to extract (~25 files):**

Core layout:
```
KoTextDocumentLayout.h/.cpp
KoTextLayoutArea.h/.cpp + KoTextLayoutArea_p.h
KoTextLayoutRootArea.h/.cpp
KoTextLayoutRootAreaProvider.h/.cpp
KoTextLayoutTableArea.h/.cpp
KoTextLayoutNoteArea.h/.cpp
KoTextLayoutEndNotesArea.h/.cpp
```

Text wrapping (needed for images):
```
KoTextLayoutObstruction.h/.cpp
RunAroundHelper.h/.cpp
AnchorStrategy.h/.cpp
FloatingAnchorStrategy.h/.cpp
InlineAnchorStrategy.h/.cpp
KoTextShapeContainerModel.h/.cpp
```

Support:
```
FrameIterator.h/.cpp
TableIterator.h/.cpp
ListItemsHelper.h/.cpp
KoTextLayoutCellHelper.h/.cpp
KoPointedAt.h/.cpp
KoCharAreaInfo.h
ToCGenerator.h/.cpp
IndexGeneratorManager.h/.cpp
DummyDocumentLayout.h/.cpp
KoStyleThumbnailer.h/.cpp
KoTextShapeData.h/.cpp
TextLayoutDebug.h/.cpp
```

**Modifications required:**
1. KoTextLayoutObstruction: Replace KoShape dependency with a simplified
   interface (ObstructionShape) that provides outline, bounds, and wrapping mode
2. AnchorStrategy family: Rewrite to work with our QGraphicsItem-based shapes
   instead of KoShape hierarchy
3. KoTextShapeData: Strip ODF loading/saving, keep QTextDocument binding
4. KoTextShapeContainerModel: Adapt to our canvas model
5. ToCGenerator: Keep mostly as-is (depends on styles and text, not shapes)

**Estimated extraction effort: 3-5 days** (adapt shape interfaces, test pagination)

---

## III. Flake Shape System Analysis

### Source: `libs/flake/` (~250 headers, ~200+ cpp files)

#### Minimum Extraction Assessment

For image shapes with interactive controls, a minimum extraction requires **35-45
files** across these tiers:

| Tier | Files | Contents |
|------|-------|----------|
| Core | 11 | KoShape, KoFlake, KoViewConverter, KoInsets, KoShapeBackground, etc. |
| Management | 6 | KoShapeManager, KoSelection, KoShapeContainer, KoShapeLayer, KoShapeGroup |
| Images | 4 | KoImageData, KoImageCollection |
| Anchoring | 2 | KoShapeAnchor, KoTextShapeDataBase |
| Canvas | 5 | KoCanvasBase, KoShapeController, resource managers |
| Stroke/Fill | 5 | KoShapeStrokeModel, KoShapeStroke, backgrounds |
| Utilities | 4 | KoPointerEvent, KoShapeApplicationData, etc. |

**External dependencies of flake:** pigmentcms, kowidgetutils, koodf, kundo2, KF6,
Qt6::Svg

#### Why NOT to Extract Flake

Even the minimum extraction (35-45 files) carries significant overhead:
- Each file has its own include web into odf, pigment, and other Calligra libs
- The tool system (selection, resize, move) is in a separate 40-file `tools/`
  directory that we'd need for interaction
- KoShape has 1247 lines -- it's a very general base class carrying SVG, ODF,
  connection point, and filter effect support we don't need
- The extraction drags in pigmentcms (color management library) as a hard
  dependency

---

## IV. Recommended Architecture: QGraphicsView Canvas

### The Insight

Instead of extracting Calligra's flake shape system for image interaction, we
use **Qt's QGraphicsView framework** as our canvas. This gives us the interaction
features we need (selection, resize, zoom, pan) without the flake dependency.

### How It Works

```
QGraphicsView (zoom, pan, scroll)
    |
    v
QGraphicsScene
    |
    +--> PageItem (custom QGraphicsRectItem per page)
    |        |
    |        +--> Renders text via QPainter using KoTextDocumentLayout
    |        +--> Page background, margins, crop marks
    |        |
    |        +--> ImageItem (custom QGraphicsPixmapItem per image)
    |                 +--> Selection handles (ResizeHandleItem)
    |                 +--> Alignment controls (floating toolbar)
    |                 +--> Text wrapping mode
    |
    +--> HeaderFooterItem (per-page overlay for headers/footers)
```

### What QGraphicsView Gives Us for Free

| Feature | Qt Class | Notes |
|---------|----------|-------|
| Zoom | QGraphicsView::scale() | Fit-width, fit-page, percentage |
| Pan/scroll | QGraphicsView::scrollContentsBy() | Smooth scrolling between pages |
| Item selection | QGraphicsItem::ItemIsSelectable | Click to select images |
| Resize handles | Custom QGraphicsItem | 8-handle resize (corners + edges) |
| Hit testing | QGraphicsScene::itemAt() | Automatic collision detection |
| Layering | QGraphicsItem::setZValue() | Images above/below text |
| Transforms | QGraphicsItem::setTransform() | Rotation, scaling per item |
| Cursor management | QGraphicsItem::setCursor() | Resize cursors on handles |
| Rubber band selection | QGraphicsView::setRubberBandSelectionMode() | Multi-select |
| Coordinate mapping | QGraphicsView::mapToScene() | Screen to document coords |

### How the Layout Engine Connects

We implement `KoTextLayoutRootAreaProvider` to bridge between the layout engine
and QGraphicsScene:

```cpp
class PageLayoutProvider : public KoTextLayoutRootAreaProvider {
    // provide() creates a root area for the next page
    // suggestRect() returns the text area of the current page
    //   (page size minus margins minus header/footer space)
    // relevantObstructions() returns obstructions for images on this page
    // doPostLayout() triggers PageItem::update() for redraw
};
```

The text wrapping subsystem (RunAroundHelper, KoTextLayoutObstruction) needs a
thin adapter to convert our QGraphicsItem-based images into obstructions:

```cpp
class ImageObstruction : public KoTextLayoutObstruction {
    // Wraps a QGraphicsPixmapItem
    // Provides outline polygon for text wrapping
    // Reports wrapping mode (tight, square, top-bottom)
};
```

### Image Interaction Model

```
ImageItem : public QGraphicsPixmapItem
    |
    +--> Properties: size, alignment (left/center/right), wrapping mode
    +--> On select: show 8 resize handles + floating alignment toolbar
    +--> On resize: update image size, trigger relayout of text
    +--> On move: update anchor position, trigger relayout
    +--> Metadata saved to per-file JSON in XDG config
```

**Estimated implementation: ~800-1200 lines** for ImageItem, ResizeHandles,
and the floating toolbar. Compare to 35-45 extracted flake files with extensive
cleaning.

---

## V. Dependency Summary: What Penelope Pulls In

### From Calligra (extracted & adapted source, in our tree)

```
PrettyCore/calligra/
    styles/
        KoCharacterStyle.h/.cpp      (stripped of ODF)
        KoParagraphStyle.h/.cpp      (stripped of ODF)
        KoListStyle.h/.cpp
        KoListLevelProperties.h/.cpp (stripped of KoImageData dep)
        KoTableStyle.h/.cpp          (stripped of ODF)
        KoTableCellStyle.h/.cpp      (stripped of ODF)
        KoTableColumnStyle.h/.cpp
        KoTableRowStyle.h/.cpp
        KoSectionStyle.h/.cpp
        KoStyleManager.h/.cpp        (stripped of ODF save)
        Styles_p.h/.cpp
    text/
        KoTextDocument.h/.cpp        (simplified)
        KoText.h/.cpp
        KoTextBlockData.h/.cpp
    layout/
        KoTextDocumentLayout.h/.cpp
        KoTextLayoutArea.h/.cpp + _p.h
        KoTextLayoutRootArea.h/.cpp
        KoTextLayoutRootAreaProvider.h/.cpp
        KoTextLayoutTableArea.h/.cpp
        KoTextLayoutNoteArea.h/.cpp
        KoTextLayoutEndNotesArea.h/.cpp
        KoTextLayoutObstruction.h/.cpp (adapted interface)
        RunAroundHelper.h/.cpp
        FrameIterator.h/.cpp
        TableIterator.h/.cpp
        ListItemsHelper.h/.cpp
        KoTextLayoutCellHelper.h/.cpp
        KoPointedAt.h/.cpp
        KoCharAreaInfo.h
        ToCGenerator.h/.cpp
        IndexGeneratorManager.h/.cpp
        DummyDocumentLayout.h/.cpp
        KoStyleThumbnailer.h/.cpp
    util/
        KoPageLayout.h/.cpp
        KoPageFormat.h/.cpp
        KoUnit.h/.cpp
        KoShadowStyle.h/.cpp
        KoBorder.h/.cpp
        KoColumns.h/.cpp

Total: ~50 files extracted, ~25,000-30,000 LOC before stripping
Estimated after stripping ODF: ~15,000-20,000 LOC
```

### From KDE Frameworks (system libraries, linked)

```
KF6::I18n              -- i18n() for translatable strings
KF6::XmlGui            -- KXmlGuiWindow, KActionCollection, XMLGUI
KF6::Config            -- KConfig, kcfg schema
KF6::ConfigWidgets     -- KConfigDialog
KF6::WidgetsAddons     -- Extra widgets
KF6::IconThemes        -- QIcon::fromTheme()
KF6::CoreAddons        -- KAboutData
KF6::KIO               -- File operations
KF6::SyntaxHighlighting -- Code block highlighting (Kate engine)
```

### From Qt (system libraries, linked)

```
Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport
```

### Third-party (bundled in our tree)

```
MD4C                   -- Markdown parser (C library, ~3000 LOC)
```

---

## VI. Risks and Mitigations

| Risk | Severity | Mitigation |
|------|----------|-----------|
| Layout engine extraction breaks due to hidden deps | High | Start extraction early; maintain a "compiles and renders hello world" checkpoint |
| KoTextLayoutObstruction can't be adapted to QGraphicsItem | Medium | Implement simplified obstruction that just provides bounding rect |
| Anchor strategies too coupled to KoShape | Medium | Start without anchored images; add run-around later |
| Style property IDs conflict with Qt's built-in IDs | Low | Calligra uses QTextFormat::UserProperty + offset; verify no collisions |
| Pagination performance on large documents | Medium | Lazy layout -- only paginate visible pages + neighbors |
| Font rendering differences between layout and canvas | Low | Both use the same QPainter/QFont stack |

---

## VII. Next Step: Planning Stage 2

With the extraction plan defined, the next planning stage is **Markdown-to-
QTextDocument Mapping** -- designing exactly how each markdown element maps to
QTextDocument structures and named Calligra styles. This involves:

1. Defining the complete set of named styles
2. Mapping MD4C callbacks to QTextDocument insertions
3. Deciding code block and image handling at the document model level
4. Designing the footnote model for paginated rendering
