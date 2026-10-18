/*
 * contentbuilder.h — MD4C → Content::Document builder
 *
 * Parses Markdown into styled content nodes for both reader modes and exports.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_CONTENTBUILDER_H
#define PENELOPE_CONTENTBUILDER_H

#include <QObject>
#include <QStack>
#include <QString>

#include <md4c.h>
#include <optional>
#include <functional>

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
    quint64 nextBlockId() { return m_nextBlockId++; }

    // The processed markdown text (after footnote extraction) used for parsing.
    // Source line ranges in blocks refer to this text.
    QString processedMarkdown() const { return m_processedMarkdown; }

    void setRenderImages(bool enabled) { m_renderImages = enabled; }
    void setSmartPunctuation(bool enabled) { m_smartPunctuation = enabled; }
    void setCancellationCheck(std::function<bool()> check) { m_cancelled = std::move(check); }
    void setBasePath(const QString &basePath);
    void setStyleManager(StyleManager *sm);
    void setHyphenator(Hyphenator *hyph);
    void setShortWords(ShortWords *sw);
    void setFootnoteStyle(const FootnoteStyle &style);

    // Parses a fragment (a note body) into blocks, sharing this builder's
    // configuration and block-id counter, without the front-matter or footnote
    // pre-passes.
    QList<Content::BlockNode> buildFragment(const QString &markdownText);

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
    // Splits a normal-text run for footnote references and ![[embeds]], then
    // splits the remaining text for #tags.
    void emitNormalText(const QString &text, int textByteOffset);
    void emitPlainText(const QString &text);
    void emitFootnoteRef(const QString &reference, int byteOffset);
    void emitEmbed(const QString &embed);

    // Style resolution
    Content::TextStyle resolveTextStyle(const QString &paraStyleName) const;
    Content::TextStyle resolveCharStyle(const QString &charStyleName) const;
    Content::ParagraphFormat resolveParagraphFormat(const QString &styleName) const;
    Content::TextStyle defaultTextStyle() const;

    // Inline node management
    void appendInlineNode(Content::InlineNode node);
    void resetState();
    void parseIntoDocument(const QString &text);
    void appendFootnoteSection();
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
    std::optional<bool> m_renderImages;
    std::function<bool()> m_cancelled;
    Content::Document m_doc;
    quint64 m_nextBlockId = 1;
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
    int m_linkSpanDepth = 0;
    bool m_smartPunctuation = true;
    bool m_inMath = false;
    bool m_mathDisplay = false;
    QList<QList<Content::FootnoteRefSite>> m_footnoteRefSites;

    // Block routing: adds a completed block to the correct container
    // The innermost entered list/quote owns each completed block.
    void addBlock(Content::BlockNode block);
    QList<Content::BlockNode> &currentBlocks();
    void closeImplicitParagraph();
    QStack<MD_BLOCKTYPE> m_containers;

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
