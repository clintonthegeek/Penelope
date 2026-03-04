# Penelope: Print & PDF Pipeline Design (Planning Stage 5)

## Architecture Overview

```
User triggers Print/PDF
    |
    v
PrintController
    |
    +---> Page Setup Dialog (page size, margins, orientation)
    +---> Header/Footer Configuration
    +---> Page Range Selection
    |
    v
QPrinter (configured with page size, resolution, output format)
    |
    v
For each page in range:
    |
    +---> Render header (separate QTextDocument per page style)
    +---> Render body (QTextDocument::drawContents with page clip)
    +---> Render footer (separate QTextDocument per page style)
    +---> QPrinter::newPage()
    |
    v
Output: Physical printer or PDF file
```

---

## PrintController Class

```cpp
class PrintController : public QObject {
    Q_OBJECT

public:
    PrintController(QTextDocument *document,
                    const PageLayoutConfig &pageLayout,
                    QObject *parent = nullptr);

    // Show print dialog and print
    void print(QWidget *parentWidget);

    // Direct PDF export (no dialog)
    void exportPdf(const QString &filePath);

    // Header/footer configuration
    void setHeaderLeft(const QString &field);
    void setHeaderCenter(const QString &field);
    void setHeaderRight(const QString &field);
    void setFooterLeft(const QString &field);
    void setFooterCenter(const QString &field);
    void setFooterRight(const QString &field);

private:
    void renderDocument(QPrinter *printer);
    void renderPage(QPainter *painter, int pageNumber, int totalPages,
                    const QRectF &headerRect, const QRectF &bodyRect,
                    const QRectF &footerRect);
    QString resolveField(const QString &field, int pageNumber,
                         int totalPages) const;

    QTextDocument *m_document;
    PageLayoutConfig m_pageLayout;
    HeaderFooterConfig m_headerFooter;
};
```

---

## Header/Footer System

Headers and footers are **extraneous to the markdown file** -- they are program
metadata stored in the per-file settings or theme. They support field
placeholders that are resolved at print time.

### Field Placeholders

| Field | Token | Resolves To |
|-------|-------|-------------|
| Page number | `{page}` | Current page number |
| Total pages | `{pages}` | Total page count |
| Page X of Y | `{page} of {pages}` | "3 of 12" |
| File name | `{filename}` | Base name of the .md file |
| File path | `{filepath}` | Full path |
| Title | `{title}` | First H1 heading in the document |
| Date | `{date}` | Current date (locale-formatted) |
| Date (custom) | `{date:yyyy-MM-dd}` | Date with custom format |
| Time | `{time}` | Current time (locale-formatted) |

### Header/Footer Rendering

```cpp
void PrintController::renderPage(QPainter *painter, int pageNumber,
                                  int totalPages,
                                  const QRectF &headerRect,
                                  const QRectF &bodyRect,
                                  const QRectF &footerRect) {
    // --- Header ---
    if (m_headerFooter.hasHeader()) {
        painter->save();
        painter->setClipRect(headerRect);

        QFont headerFont = m_headerFooter.headerFont();
        painter->setFont(headerFont);
        painter->setPen(m_headerFooter.headerColor());

        QString left = resolveField(m_headerFooter.headerLeft(),
                                     pageNumber, totalPages);
        QString center = resolveField(m_headerFooter.headerCenter(),
                                       pageNumber, totalPages);
        QString right = resolveField(m_headerFooter.headerRight(),
                                      pageNumber, totalPages);

        painter->drawText(headerRect, Qt::AlignLeft | Qt::AlignVCenter, left);
        painter->drawText(headerRect, Qt::AlignHCenter | Qt::AlignVCenter, center);
        painter->drawText(headerRect, Qt::AlignRight | Qt::AlignVCenter, right);

        // Optional separator line
        if (m_headerFooter.headerSeparator()) {
            painter->setPen(QPen(m_headerFooter.separatorColor(), 0.5));
            painter->drawLine(headerRect.bottomLeft(), headerRect.bottomRight());
        }
        painter->restore();
    }

    // --- Body ---
    painter->save();
    painter->translate(bodyRect.topLeft());
    painter->translate(0, -pageNumber * bodyRect.height());

    QRectF clipRect(0, pageNumber * bodyRect.height(),
                    bodyRect.width(), bodyRect.height());
    m_document->drawContents(painter, clipRect);
    painter->restore();

    // --- Footer ---
    if (m_headerFooter.hasFooter()) {
        painter->save();
        painter->setClipRect(footerRect);
        // Same pattern as header...
        painter->restore();
    }
}
```

### Page Geometry Division

```
+------------------------------------------+
|              Top Margin                    |
+------------------------------------------+
|  Header Left | Header Center | Header Right|
+------------------------------------------+
|         Header Separator Line             |
+------------------------------------------+
|                                           |
|                                           |
|              Body Content                  |
|          (QTextDocument page N)            |
|                                           |
|                                           |
+------------------------------------------+
|         Footer Separator Line             |
+------------------------------------------+
|  Footer Left | Footer Center | Footer Right|
+------------------------------------------+
|              Bottom Margin                 |
+------------------------------------------+
```

The body area is the page size minus margins minus header/footer height.
The QTextDocument's page size must be set to the body area dimensions so
that pagination occurs at the correct points.

```cpp
void PrintController::renderDocument(QPrinter *printer) {
    QPainter painter(printer);

    QRectF pageRect = printer->pageRect(QPrinter::Point);
    QMarginsF margins = m_pageLayout.margins();  // in points

    qreal headerHeight = m_headerFooter.hasHeader() ? 30.0 : 0.0;
    qreal footerHeight = m_headerFooter.hasFooter() ? 20.0 : 0.0;

    QRectF headerRect(margins.left(), margins.top(),
                      pageRect.width() - margins.left() - margins.right(),
                      headerHeight);

    QRectF bodyRect(margins.left(),
                    margins.top() + headerHeight + 8.0,
                    pageRect.width() - margins.left() - margins.right(),
                    pageRect.height() - margins.top() - margins.bottom()
                        - headerHeight - footerHeight - 16.0);

    QRectF footerRect(margins.left(),
                      pageRect.height() - margins.bottom() - footerHeight,
                      pageRect.width() - margins.left() - margins.right(),
                      footerHeight);

    // Set document page size to body area
    m_document->setPageSize(bodyRect.size());
    int totalPages = m_document->pageCount();

    for (int page = 0; page < totalPages; ++page) {
        renderPage(&painter, page, totalPages,
                   headerRect, bodyRect, footerRect);

        if (page < totalPages - 1)
            printer->newPage();
    }

    painter.end();
}
```

---

## Print Dialog

```cpp
void PrintController::print(QWidget *parentWidget) {
    QPrinter printer(QPrinter::HighResolution);

    // Apply page layout from settings
    printer.setPageSize(QPageSize(m_pageLayout.pageSize()));
    printer.setPageOrientation(m_pageLayout.orientation());
    printer.setPageMargins(m_pageLayout.margins(), QPageLayout::Point);

    QPrintDialog dialog(&printer, parentWidget);
    dialog.setWindowTitle(QObject::tr("Print Document"));
    dialog.setOption(QAbstractPrintDialog::PrintPageRange);
    dialog.setMinMax(1, m_document->pageCount());

    if (dialog.exec() == QDialog::Accepted) {
        renderDocument(&printer);
    }
}
```

---

## PDF Export

```cpp
void PrintController::exportPdf(const QString &filePath) {
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);

    printer.setPageSize(QPageSize(m_pageLayout.pageSize()));
    printer.setPageOrientation(m_pageLayout.orientation());
    printer.setPageMargins(m_pageLayout.margins(), QPageLayout::Point);

    // PDF metadata
    printer.setCreator(QStringLiteral("Penelope"));
    printer.setDocName(m_document->metaInformation(QTextDocument::DocumentTitle));

    renderDocument(&printer);
}
```

---

## Table of Contents Generation

TOC is generated by scanning the document for heading blocks and recording their
page numbers. It can be:

1. **Printed as a prefix** -- TOC rendered as additional pages before the body
2. **Displayed in a dock** -- Navigable outline in a side panel
3. **Both**

### TOC Data Collection

```cpp
struct TocEntry {
    int level;        // 1-6
    QString text;     // heading text
    int pageNumber;   // resolved after pagination
};

QVector<TocEntry> PrintController::generateToc() const {
    QVector<TocEntry> entries;

    for (QTextBlock block = m_document->begin();
         block != m_document->end(); block = block.next()) {
        int level = block.blockFormat().headingLevel();
        if (level > 0 && level <= 6) {
            // Determine which page this block falls on
            QRectF blockRect = m_document->documentLayout()->blockBoundingRect(block);
            int page = qFloor(blockRect.top() / m_document->pageSize().height());

            entries.append({level, block.text(), page});
        }
    }
    return entries;
}
```

### TOC Rendering (Print Prefix)

A separate QTextDocument is constructed containing the TOC entries with
dot-leaders and page numbers. It's printed before the body pages, and
total page count includes the TOC pages.

```cpp
QTextDocument* PrintController::buildTocDocument(
        const QVector<TocEntry> &entries, int tocPageOffset) {
    auto *tocDoc = new QTextDocument();
    QTextCursor cursor(tocDoc);

    // Title
    QTextBlockFormat titleFmt;
    titleFmt.setHeadingLevel(1);
    QTextCharFormat titleCharFmt;
    titleCharFmt.setFontWeight(QFont::Bold);
    titleCharFmt.setFontPointSize(18);
    cursor.setBlockFormat(titleFmt);
    cursor.setCharFormat(titleCharFmt);
    cursor.insertText(tr("Table of Contents"));

    // Entries
    for (const auto &entry : entries) {
        cursor.insertBlock();
        QTextBlockFormat entryFmt;
        entryFmt.setLeftMargin((entry.level - 1) * 20.0);
        cursor.setBlockFormat(entryFmt);

        QTextCharFormat textFmt;
        textFmt.setFontPointSize(11);
        cursor.setCharFormat(textFmt);
        cursor.insertText(entry.text);

        // Page number (right-aligned via tab stop)
        // Actual page = entry.pageNumber + tocPageOffset
        cursor.insertText(QString("\t%1").arg(
            entry.pageNumber + tocPageOffset + 1));
    }

    return tocDoc;
}
```

---

## Header/Footer Configuration Storage

Stored in the theme JSON and/or per-file metadata:

```json
{
    "headerFooter": {
        "headerEnabled": true,
        "headerLeft": "",
        "headerCenter": "{title}",
        "headerRight": "",
        "headerFont": "Noto Sans, 9pt",
        "headerColor": "#888888",
        "headerSeparator": true,
        "footerEnabled": true,
        "footerLeft": "{date}",
        "footerCenter": "",
        "footerRight": "Page {page} of {pages}",
        "footerFont": "Noto Sans, 9pt",
        "footerColor": "#888888",
        "footerSeparator": true,
        "separatorColor": "#cccccc"
    }
}
```

The style dock's Page section includes a sub-section for header/footer
configuration with text fields for each position (left/center/right) and a
dropdown of available field tokens.

---

## Print/PDF Dock Integration

The style dock's Page section provides:

```
Page Section (collapsible)
├── Page Size: [A4 ▼]
├── Orientation: (●) Portrait  ( ) Landscape
├── Margins
│   ├── Top: [25.0] mm
│   ├── Bottom: [25.0] mm
│   ├── Left: [30.0] mm
│   └── Right: [25.0] mm
├── Header/Footer (sub-collapsible)
│   ├── [✓] Enable Header
│   ├── Left: [________] Center: [{title}__] Right: [________]
│   ├── [✓] Enable Footer
│   ├── Left: [{date}___] Center: [________] Right: [{page}/{pages}]
│   ├── Font: [Noto Sans ▼] Size: [9 ▼]
│   └── [✓] Separator lines
└── Table of Contents
    ├── [✓] Include TOC when printing
    └── TOC depth: [3 ▼] (H1-H3)
```

All of these settings are part of the theme (so different themes can have
different page layouts) and can be overridden per-file in the metadata.

---

## CMake Dependencies

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)
target_link_libraries(PrettyPrint PRIVATE Qt6::PrintSupport)
```
