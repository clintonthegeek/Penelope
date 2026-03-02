/*
 * contentmodel.h — Content node types (header-only, std::variant)
 *
 * Defines the intermediate representation between Markdown parsing
 * and the layout engine. All style information is resolved at build time.
 *
 * NOTE: Qt GUI headers (QColor, QImage) must be included BEFORE
 * opening the Content namespace to avoid ADL issues with Qt6 macros.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PRETTYREADER_CONTENTMODEL_H
#define PRETTYREADER_CONTENTMODEL_H

#include <QColor>
#include <QList>
#include <QMarginsF>
#include <QString>
#include <QStringList>
#include <Qt>

#include <variant>

namespace Content {

// --- Source position tracking ---

struct SourceRange {
    int startLine = -1;  // 1-based line in markdown source
    int endLine = -1;    // 1-based line in markdown source
};

// --- Style structs ---

struct TextStyle {
    QString fontFamily = QStringLiteral("Noto Serif");
    qreal fontSize = 11.0;
    int fontWeight = 400;       // QFont::Weight values
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    QColor foreground = QColor(0x1a, 0x1a, 0x1a);
    QColor background;          // invalid = transparent
    qreal letterSpacing = 0;
    bool superscript = false;
    bool subscript = false;
    QStringList fontFeatures;   // e.g. {"liga", "kern", "onum"}
    QString linkHref;           // non-empty when this run is inside <a href="...">
};

struct ParagraphFormat {
    Qt::Alignment alignment = Qt::AlignLeft;
    qreal spaceBefore = 0;
    qreal spaceAfter = 0;
    qreal leftMargin = 0;
    qreal rightMargin = 0;
    qreal firstLineIndent = 0;
    qreal lineHeightPercent = 100;
    QColor background;          // invalid = transparent
    int headingLevel = 0;       // 0 = not a heading, 1-6
};

// --- Inline nodes ---

struct TextRun {
    QString text;
    TextStyle style;
};

struct InlineCode {
    QString text;
    TextStyle style;
};

struct Link {
    QString href;
    QString tooltip;
    QString text;   // display text (flattened from children)
    TextStyle style;
};

struct InlineImage {
    QString src;
    QByteArray resolvedImageData; // raw image bytes (PNG/JPEG)
    QString altText;
    qreal width = 0;   // 0 = auto
    qreal height = 0;
};

struct FootnoteRef {
    int index = 0;
    QString label;
    TextStyle style;
};

struct SoftBreak {};
struct HardBreak {};

// InlineNode variant
using InlineNode = std::variant<
    TextRun,
    InlineCode,
    Link,
    InlineImage,
    FootnoteRef,
    SoftBreak,
    HardBreak
>;

// --- Block nodes ---

struct Paragraph {
    ParagraphFormat format;
    QList<InlineNode> inlines;
    SourceRange source;
};

struct Heading {
    int level = 1;
    ParagraphFormat format;
    QList<InlineNode> inlines;
    SourceRange source;
};

struct CodeBlock {
    QString language;
    QString code;
    TextStyle style;
    QColor background = QColor(0xf6, 0xf8, 0xfa);
    qreal padding = 8.0;
    bool isFenced = true; // false for 4-space indented code blocks
    int lineHeightPercent = 140; // from CodeBlock paragraph style
    SourceRange source;
};

// Forward-declare BlockNode for recursive types
struct BlockQuote;
struct List;
using BlockNode = std::variant<
    Paragraph,
    Heading,
    CodeBlock,
    struct BlockQuote,
    struct List,
    struct Table,
    struct HorizontalRule,
    struct FootnoteSection
>;

struct BlockQuote {
    int level = 1;
    QList<BlockNode> children;
    ParagraphFormat format;
};

struct ListItem {
    QList<BlockNode> children;
    bool isTask = false;
    bool taskChecked = false;
};

enum class ListType { Unordered, Ordered };

struct List {
    ListType type = ListType::Unordered;
    int startNumber = 1;
    QList<ListItem> items;
    SourceRange source;
};

struct TableCell {
    QList<InlineNode> inlines;
    Qt::Alignment alignment = Qt::AlignLeft;
    bool isHeader = false;
    QColor background;
    TextStyle style;
};

struct TableRow {
    QList<TableCell> cells;
};

struct Table {
    QList<TableRow> rows;
    int headerRowCount = 0;
    SourceRange source;
    // Table styling
    QColor headerBackground;
    QColor headerForeground;
    QColor bodyBackground;
    QColor alternateRowColor;
    qreal cellPadding = 4.0;
    // Outer border
    qreal borderWidth = 0.5;
    QColor borderColor = QColor(0xdd, 0xdd, 0xdd);
    // Inner grid border
    qreal innerBorderWidth = 0.5;
    QColor innerBorderColor = QColor(0xcc, 0xcc, 0xcc);
    // Header bottom border (heavier line under header row)
    qreal headerBottomBorderWidth = 2.0;
    QColor headerBottomBorderColor = QColor(0x33, 0x33, 0x33);
};

struct HorizontalRule {
    qreal topMargin = 12.0;
    qreal bottomMargin = 12.0;
    SourceRange source;
};

struct Footnote {
    QString label;
    QList<InlineNode> content;
    TextStyle numberStyle;
    TextStyle textStyle;
};

struct FootnoteSection {
    QList<Footnote> footnotes;
    bool showSeparator = true;
    qreal separatorLength = 0.33; // fraction of page width
};

// --- Document ---

struct Document {
    QList<BlockNode> blocks;
};

} // namespace Content

#endif // PRETTYREADER_CONTENTMODEL_H
