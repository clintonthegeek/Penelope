/*
 * contentbuilder.h — MD4C → Content::Document builder
 *
 * Same callback structure as DocumentBuilder, but emits Content:: nodes
 * instead of QTextCursor operations.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_CONTENTBUILDER_H
#define PENELOPE_CONTENTBUILDER_H

#include <QObject>
#include <QStack>
#include <QString>

#include <md4c.h>

#include "contentmodel.h"
#include "footnotestyle.h"

class Hyphenator;
class ShortWords;
class StyleManager;

class ContentBuilder : public QObject {
    Q_OBJECT
public:
    explicit ContentBuilder(QObject *parent = nullptr);

    Content::Document build(const QString &markdownText);

    // The processed markdown text (after footnote extraction) used for parsing.
    // Source line ranges in blocks refer to this text.
    QString processedMarkdown() const { return m_processedMarkdown; }

    void setBasePath(const QString &basePath);
    void setStyleManager(StyleManager *sm);
    void setHyphenator(Hyphenator *hyph);
    void setShortWords(ShortWords *sw);
    void setFootnoteStyle(const FootnoteStyle &style);

private:
    // MD4C static callbacks
    static int sEnterBlock(MD_BLOCKTYPE type, void *detail, void *userdata);
    static int sLeaveBlock(MD_BLOCKTYPE type, void *detail, void *userdata);
    static int sEnterSpan(MD_SPANTYPE type, void *detail, void *userdata);
    static int sLeaveSpan(MD_SPANTYPE type, void *detail, void *userdata);
    static int sText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                     void *userdata);

    // Instance handlers
    int enterBlock(MD_BLOCKTYPE type, void *detail);
    int leaveBlock(MD_BLOCKTYPE type, void *detail);
    int enterSpan(MD_SPANTYPE type, void *detail);
    int leaveSpan(MD_SPANTYPE type, void *detail);
    int onText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size);

    // Helpers
    QString extractAttribute(const MD_ATTRIBUTE &attr);
    QString resolveEntity(const QString &entity);
    QString processTypography(const QString &text) const;

    // Style resolution
    Content::TextStyle resolveTextStyle(const QString &paraStyleName) const;
    Content::TextStyle resolveCharStyle(const QString &charStyleName) const;
    Content::ParagraphFormat resolveParagraphFormat(const QString &styleName) const;
    Content::TextStyle defaultTextStyle() const;

    // Inline node management
    void appendInlineNode(Content::InlineNode node);
    QList<Content::InlineNode> *currentInlines();

    // Source position tracking
    int byteOffsetToLine(int offset) const;
    struct BlockTracker {
        int firstByteOffset = -1;
        int lastByteEnd = -1;  // exclusive: offset + size
    };
    QStack<BlockTracker> m_blockTrackers;
    QList<int> m_lineStartOffsets; // byte offset where each line starts
    const char *m_bufferStart = nullptr;

    // State
    Content::Document m_doc;
    QString m_basePath;
    StyleManager *m_styleManager = nullptr;
    Hyphenator *m_hyphenator = nullptr;
    ShortWords *m_shortWords = nullptr;

    // Current inline target stack (for nested blocks like blockquote > paragraph)
    QStack<QList<Content::InlineNode> *> m_inlineStack;

    // Current style stack for spans
    QStack<Content::TextStyle> m_styleStack;
    Content::TextStyle m_currentStyle;

    // Link tracking (href flows through TextStyle on each TextRun inside the span)
    QString m_linkHref;

    // Block routing: adds a completed block to the correct container
    // (blockquote stack > list stack > doc.blocks)
    void addBlock(Content::BlockNode block);

    // Block tracking
    int m_blockQuoteLevel = 0;
    QStack<QList<Content::BlockNode>> m_blockQuoteStack;
    bool m_inCodeBlock = false;
    bool m_codeFenced = true;
    QString m_codeLanguage;
    QString m_codeText;

    // List tracking
    struct ListInfo {
        Content::ListType type;
        int startNumber;
        QList<Content::ListItem> items;
        bool hasImplicitParagraph = false; // current item has implicit para on inline stack
    };
    QStack<ListInfo> m_listStack;

    // Table tracking
    bool m_inTable = false;
    bool m_inTableHeader = false;
    QList<Content::TableRow> m_tableRows;
    QList<Content::TableCell> m_currentRowCells;
    QList<Qt::Alignment> m_tableColumnAligns;
    int m_tableCol = 0;

    // Image tracking
    bool m_collectingAltText = false;
    QString m_altText;
    QString m_imageSrc;
    QString m_imageTitle;

    // Footnotes
    struct ParsedFootnote {
        QString label;
        QString text;
    };
    QList<ParsedFootnote> m_footnotes;
    FootnoteStyle m_footnoteStyle;

    QString m_processedMarkdown;
};

#endif // PENELOPE_CONTENTBUILDER_H
