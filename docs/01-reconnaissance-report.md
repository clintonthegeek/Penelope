# Penelope: Reconnaissance Report

## Overview

This document summarizes the investigation of four codebases conducted as
groundwork for Penelope, a markdown reading application with rich styling,
paginated rendering, and first-class print/PDF support.

Sources investigated:
- **Calligra Words** (`kdesrc/calligra/`) -- Document model, style system, text
  layout, ODF, printing
- **QOwnNotes** (`QOwnNotes/`) -- Markdown editor library, parsing, highlighting
- **KateMDI** (`kdesrc/kate/`) -- Multi-document interface, sidebar/dock system
- **PlanStanLite** (`refapp/`) -- Reference Qt6/KF6 application for build system
  and architecture patterns

---

## I. Calligra Words Document Model

### Architecture

Calligra is built as a layered library stack:

```
libs/plugin -> libs/odf -> libs/store -> libs/flake -> libs/text -> libs/textlayout -> libs/main -> words/part
```

The document model centers on Qt's QTextDocument with Calligra wrappers providing
style management, ODF serialization, and advanced layout. Requirements: Qt 6.5+,
KDE Frameworks 6.0+, C++20.

### Key Classes

| Layer | Class | Role |
|-------|-------|------|
| Document | KWDocument | Top-level Words document. Manages pages, frames, styles |
| Text Model | KoTextDocument | Wrapper around QTextDocument. Provides style manager, text editor, change tracker access |
| Editing | KoTextEditor | High-level cursor/selection/formatting API over QTextCursor |
| Style | KoStyleManager | Central registry for all named styles |
| Style | KoCharacterStyle | ~90+ character properties (font, color, weight, etc.) |
| Style | KoParagraphStyle | Paragraph spacing, indents, borders, lists. Inherits KoCharacterStyle |
| Style | KoListStyle | Bullet/numbering per level |
| Style | KoTableStyle/Row/Column/Cell | Full table formatting |
| Layout | KoTextDocumentLayout | Custom QAbstractTextDocumentLayout. Line breaking, pagination, widow/orphan control, drop caps |
| Layout | KoTextLayoutRootArea / KoTextLayoutArea | Bounded layout areas representing page columns |
| Page | KoPageLayout | Page size, orientation, margins, padding, borders |
| Page | KWPageStyle | Master page style (headers/footers/columns/margins) |
| Page | KWPageManager | Collection of page styles, generates page instances |
| Frames | KWTextFrameSet | Links a QTextDocument to a set of page regions |
| ODF | KoTextLoader / KoTextWriter | ODF text element parsing/serialization |
| Print | KoPrintingDialog / KWPrintingDialog | Page iteration, shape rendering to QPainter/QPrinter |

### View/Widget Layer

All QtWidgets, no QML:
- `KWCanvas` -- QWidget rendering frames via KoShapeManager::paint()
- `KWView` -- Main view (inherits KoView = QWidget + KXMLGUIClient)
- `KWViewModeNormal` / `KWViewModePreview` -- Strategy for editing vs read-only

### Style System

Styles stored as QVariant properties in QTextCharFormat/QTextBlockFormat using
custom property IDs. They support inheritance (child references parent), ODF
serialization via KoGenStyle, and change tracking.

### Print/PDF Pipeline

1. KWPrintingDialog iterates through page range
2. Each page calls preparePage() to set up layout
3. Shapes rendered via KoShapeManager::paint() to QPainter
4. PDF export via QPrinter::setOutputFormat(QPrinter::PdfFormat)

### System Library Viability

**Not viable as a system library.** Calligra is not packaged as a standalone SDK.
Its internal shared libraries are not designed for external consumption. No stable
public API or ABI guarantee.

### Extractable Components

| Component | Difficulty | Value |
|-----------|-----------|-------|
| KoCharacterStyle / KoParagraphStyle / KoStyleManager | Medium | Very High |
| KoTextDocumentLayout / KoTextLayoutArea | Hard | Very High |
| KoPageLayout / KoPageFormat | Easy | High |
| KoPrintingDialog pattern | Easy | High |
| KoTextEditor | Medium | Medium |
| Header/Footer frame system | Hard | High |

The style system and layout engine can work independently of KWDocument, operating
directly on QTextDocument:

```cpp
QTextDocument *doc = new QTextDocument();
KoTextDocument koDoc(doc);
koDoc.setStyleManager(new KoStyleManager());
KoTextDocumentLayout *layout = new KoTextDocumentLayout(doc);
doc->setDocumentLayout(layout);
```

---

## II. QOwnNotes Markdown Editor Library

### Library Contents (~14K lines)

| Class | Purpose |
|-------|---------|
| QMarkdownTextEdit | QPlainTextEdit derivative with markdown features |
| MarkdownHighlighter | QSyntaxHighlighter with 28 element states, regex-based |
| LineNumArea | Line number widget with bookmarks |
| QPlainTextEditSearchWidget | Search/replace overlay |
| CodeToHtmlConverter | Syntax-highlighted HTML from code blocks (28+ languages) |

### Parsing Approach

Not a true parser -- a regex-based, line-by-line state machine highlighter. No AST.
Each QTextBlock gets a HighlighterState enum. Designed for real-time visual feedback,
not semantic understanding.

Supported elements: H1-H6, bold, italic, strikethrough, underline, inline code,
fenced code blocks, links, images, lists, block quotes, tables, horizontal rules,
checkboxes, comments, trailing spaces.

### Reusable Components

| Component | Reusable? | Notes |
|-----------|-----------|-------|
| MarkdownHighlighter | Yes, directly | Works on any QTextDocument |
| Regex patterns | Yes | Heading/list/link/emphasis detection |
| Language keyword data | Yes | 28 language definitions |
| CodeToHtmlConverter | Yes | Standalone code block rendering |
| Search widget | Adaptable | QPlainTextEdit API similar in QTextEdit |
| Bracket auto-closing | Yes | Pure logic |

### QOwnNotes Three-Layer Architecture

1. **Editing**: QMarkdownTextEdit (plain text + highlighting)
2. **Parsing**: MD4C library (CommonMark + GitHub extensions, wiki links, LaTeX
   math, underline, task lists)
3. **Rendering**: QLiteHtmlWidget (litehtml for HTML/CSS display)

The conversion pipeline in Note::textToMarkdownHtml() (~484 lines) handles
frontmatter, URLs, code highlighting, MD4C parsing, image processing, CSS
injection, and caching.

---

## III. KateMDI (Multi-Document + Docks)

### Architecture (~2750 lines in katemdi.h/.cpp)

Four classes in the KateMDI namespace:
- `MainWindow` -- inherits KParts::MainWindow, manages 4 sidebars
- `Sidebar` -- QSplitter-based, contains MultiTabBar + QStackedWidget
- `ToolView` -- individual panel widget with icon, text, toolbar
- `GUIClient` -- action management for toggle visibility

### Sidebar System

Four cardinal sidebars (Left, Right, Top, Bottom), each collapsible. Uses
KMultiTabBar from KDE WidgetsAddons. Tool views registered by ID with icon and
position. Full session save/restore.

### Reusability

Heavily KDE-dependent (KParts, KMultiTabBar, KXMLGUIClient, KConfig). The pattern
is sound but extraction requires replacing:

| KDE Component | Qt Replacement | Effort |
|---------------|---------------|--------|
| KParts::MainWindow | QMainWindow | Easy |
| KMultiTabBar | Custom vertical tab widget | ~500-800 lines |
| KXMLGUIClient | QAction/QMenu | Easy but manual |
| KConfig | QSettings or JSON | Easy |

Since Penelope embraces KDE dependencies, most of this becomes moot -- we can
use KDE's widgets directly rather than replacing them.

---

## IV. Reference App (PlanStanLite)

### Overview

~83K-line C++20 / Qt6 / KF6 calendar application. GPL licensed.

### Key Patterns to Reuse

- **CMake**: qt_standard_project_setup(), CMAKE_AUTOMOC, ECM integration,
  monolithic core library with flat include paths, KConfig kcfg schema generation
- **Architecture**: MVC + Command pattern (QUndoStack), modular editors,
  controller-per-document
- **MDI**: QMdiArea with QDockWidget panels (explorer, editor, date picker, tags)
- **KDE Integration**: KXmlGuiWindow, KConfig/kcfg, XMLGUI for menus/toolbars
- **Signals/Slots**: Heavy use for loose coupling between layers

### Build System Highlights

```cmake
cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
qt_standard_project_setup()

find_package(ECM 6.0 REQUIRED NO_MODULE)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets)
find_package(KF6XmlGui REQUIRED)
find_package(KF6Config REQUIRED)
# etc.

add_library(PlanStanCore ... )   # Monolithic core library
qt_add_executable(PlanStanLite src/main.cpp)
target_link_libraries(PlanStanLite PRIVATE PlanStanCore ...)
```
