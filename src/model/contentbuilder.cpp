/*
 * contentbuilder.cpp — MD4C → Content::Document builder
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "contentbuilder.h"
#include "stylemanager.h"
#include "paragraphstyle.h"
#include "characterstyle.h"
#include "fontfeatures.h"
#include "tablestyle.h"
#include "hyphenator.h"
#include "shortwords.h"
#include "footnoteparser.h"

#include <algorithm>

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>

ContentBuilder::ContentBuilder(QObject *parent)
    : QObject(parent)
{
}

// --- Style resolution helpers ---

Content::TextStyle ContentBuilder::defaultTextStyle() const
{
    Content::TextStyle s;
    s.fontFamily = QStringLiteral("Noto Serif");
    s.fontSize = 11.0;
    s.fontWeight = 400;
    s.italic = false;
    s.foreground = QColor(0x1a, 0x1a, 0x1a);
    return s;
}

Content::TextStyle ContentBuilder::resolveTextStyle(const QString &paraStyleName) const
{
    Content::TextStyle s = defaultTextStyle();
    if (!m_styleManager)
        return s;
    ParagraphStyle ps = m_styleManager->resolvedParagraphStyle(paraStyleName);
    if (ps.hasFontFamily()) s.fontFamily = ps.fontFamily();
    if (ps.hasFontSize()) s.fontSize = ps.fontSize();
    if (ps.hasFontWeight()) s.fontWeight = static_cast<int>(ps.fontWeight());
    if (ps.hasFontItalic()) s.italic = ps.fontItalic();
    if (ps.hasForeground()) s.foreground = ps.foreground();
    if (ps.hasFontFeatures()) {
        s.fontFeatures = FontFeatures::toStringList(ps.fontFeatures());
    }
    return s;
}

Content::TextStyle ContentBuilder::resolveCharStyle(const QString &charStyleName) const
{
    Content::TextStyle s = m_currentStyle; // inherit from current
    if (!m_styleManager)
        return s;
    CharacterStyle cs = m_styleManager->resolvedCharacterStyle(charStyleName);
    if (cs.hasFontFamily()) s.fontFamily = cs.fontFamily();
    if (cs.hasFontSize()) s.fontSize = cs.fontSize();
    if (cs.hasFontWeight()) s.fontWeight = static_cast<int>(cs.fontWeight());
    if (cs.hasFontItalic()) s.italic = cs.fontItalic();
    if (cs.hasFontUnderline()) s.underline = cs.fontUnderline();
    if (cs.hasFontStrikeOut()) s.strikethrough = cs.fontStrikeOut();
    if (cs.hasForeground()) s.foreground = cs.foreground();
    if (cs.hasBackground()) s.background = cs.background();
    if (cs.hasLetterSpacing()) s.letterSpacing = cs.letterSpacing();
    if (cs.hasFontFeatures()) {
        s.fontFeatures = FontFeatures::toStringList(cs.fontFeatures());
    }
    return s;
}

Content::ParagraphFormat ContentBuilder::resolveParagraphFormat(const QString &styleName) const
{
    Content::ParagraphFormat f;
    if (!m_styleManager)
        return f;
    ParagraphStyle ps = m_styleManager->resolvedParagraphStyle(styleName);
    if (ps.hasAlignment()) f.alignment = ps.alignment();
    if (ps.hasSpaceBefore()) f.spaceBefore = ps.spaceBefore();
    if (ps.hasSpaceAfter()) f.spaceAfter = ps.spaceAfter();
    if (ps.hasLeftMargin()) f.leftMargin = ps.leftMargin();
    if (ps.hasRightMargin()) f.rightMargin = ps.rightMargin();
    if (ps.hasFirstLineIndent()) f.firstLineIndent = ps.firstLineIndent();
    if (ps.hasLineHeight()) f.lineHeightPercent = ps.lineHeightPercent();
    if (ps.hasBackground()) f.background = ps.background();
    f.headingLevel = ps.headingLevel();
    return f;
}

// --- Block routing ---

void ContentBuilder::addBlock(Content::BlockNode block)
{
    if (!m_blockQuoteStack.isEmpty()) {
        m_blockQuoteStack.top().append(std::move(block));
    } else if (!m_listStack.isEmpty() && !m_listStack.top().items.isEmpty()) {
        m_listStack.top().items.last().children.append(std::move(block));
    } else {
        m_doc.blocks.append(std::move(block));
    }
}

// --- Inline management ---

void ContentBuilder::appendInlineNode(Content::InlineNode node)
{
    if (auto *inlines = currentInlines()) {
        inlines->append(std::move(node));
        return;
    }

    // Tight list item: MD4C didn't emit MD_BLOCK_P, create implicit paragraph
    if (!m_listStack.isEmpty()) {
        auto &info = m_listStack.top();
        if (!info.items.isEmpty()) {
            Content::ParagraphFormat fmt;
            if (m_styleManager)
                fmt = resolveParagraphFormat(QStringLiteral("ListItem"));
            auto &children = info.items.last().children;
            children.append(Content::Paragraph{fmt, {}});
            auto *p = std::get_if<Content::Paragraph>(&children.last());
            m_inlineStack.push(&p->inlines);
            info.hasImplicitParagraph = true;
            p->inlines.append(std::move(node));
        }
    }
}

QList<Content::InlineNode> *ContentBuilder::currentInlines()
{
    if (m_inlineStack.isEmpty())
        return nullptr;
    return m_inlineStack.top();
}

// --- Typography ---

QString ContentBuilder::processTypography(const QString &text) const
{
    if (text.isEmpty())
        return text;
    QString result = text;
    if (m_shortWords)
        result = m_shortWords->process(result);
    if (m_hyphenator && m_hyphenator->isLoaded())
        result = m_hyphenator->hyphenateText(result);
    return result;
}

// --- Build entry point ---

Content::Document ContentBuilder::build(const QString &markdownText)
{
    m_doc = Content::Document{};
    m_inlineStack.clear();
    m_styleStack.clear();
    m_listStack.clear();
    m_blockQuoteLevel = 0;
    m_blockQuoteStack.clear();
    m_inCodeBlock = false;
    m_codeLanguage.clear();
    m_codeText.clear();
    m_inTable = false;
    m_inTableHeader = false;
    m_tableRows.clear();
    m_currentRowCells.clear();
    m_tableColumnAligns.clear();
    m_tableCol = 0;
    m_collectingAltText = false;
    m_altText.clear();
    m_linkHref.clear();
    m_footnotes.clear();

    // Resolve default text style
    if (m_styleManager)
        m_currentStyle = resolveTextStyle(QStringLiteral("Default Paragraph Style"));
    else
        m_currentStyle = defaultTextStyle();

    // Extract footnotes
    FootnoteParser fnParser;
    QString processed = fnParser.process(markdownText);
    for (const auto &fn : fnParser.footnotes()) {
        m_footnotes.append({fn.label, fn.content});
    }

    // Store processed text for source line extraction
    m_processedMarkdown = processed;

    // Build line offset table for source tracking
    const QByteArray utf8 = processed.toUtf8();
    m_lineStartOffsets.clear();
    m_lineStartOffsets.append(0); // line 1 starts at byte 0
    for (int i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '\n')
            m_lineStartOffsets.append(i + 1);
    }
    m_bufferStart = utf8.constData();
    m_blockTrackers.clear();

    // Parse
    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_UNDERLINE
                 | MD_FLAG_WIKILINKS | MD_FLAG_LATEXMATHSPANS;
    parser.enter_block = &ContentBuilder::sEnterBlock;
    parser.leave_block = &ContentBuilder::sLeaveBlock;
    parser.enter_span = &ContentBuilder::sEnterSpan;
    parser.leave_span = &ContentBuilder::sLeaveSpan;
    parser.text = &ContentBuilder::sText;

    md_parse(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), &parser, this);

    // Append footnote section
    if (!m_footnotes.isEmpty()) {
        Content::FootnoteSection section;
        section.showSeparator = m_footnoteStyle.showSeparator;
        section.separatorLength = m_footnoteStyle.separatorLength;
        for (int i = 0; i < m_footnotes.size(); ++i) {
            Content::Footnote fn;
            fn.label = m_footnoteStyle.formatNumber(m_footnoteStyle.startNumber + i);

            Content::TextStyle numStyle = m_currentStyle;
            numStyle.fontSize = 8.0;
            numStyle.foreground = QColor(0x03, 0x66, 0xd6);
            numStyle.superscript = m_footnoteStyle.superscriptNote;
            fn.numberStyle = numStyle;

            Content::TextStyle textStyle = m_currentStyle;
            textStyle.fontSize = 9.0;
            textStyle.foreground = QColor(0x55, 0x55, 0x55);
            fn.textStyle = textStyle;

            Content::TextRun textRun;
            textRun.text = m_footnotes[i].text;
            textRun.style = textStyle;
            fn.content.append(textRun);

            section.footnotes.append(fn);
        }
        m_doc.blocks.append(section);
    }

    return m_doc;
}

// --- Setters ---

void ContentBuilder::setBasePath(const QString &basePath) { m_basePath = basePath; }
void ContentBuilder::setStyleManager(StyleManager *sm) { m_styleManager = sm; }
void ContentBuilder::setHyphenator(Hyphenator *hyph) { m_hyphenator = hyph; }
void ContentBuilder::setShortWords(ShortWords *sw) { m_shortWords = sw; }
void ContentBuilder::setFootnoteStyle(const FootnoteStyle &style) { m_footnoteStyle = style; }

// --- Static callbacks ---

int ContentBuilder::sEnterBlock(MD_BLOCKTYPE type, void *detail, void *userdata)
{ return static_cast<ContentBuilder *>(userdata)->enterBlock(type, detail); }
int ContentBuilder::sLeaveBlock(MD_BLOCKTYPE type, void *detail, void *userdata)
{ return static_cast<ContentBuilder *>(userdata)->leaveBlock(type, detail); }
int ContentBuilder::sEnterSpan(MD_SPANTYPE type, void *detail, void *userdata)
{ return static_cast<ContentBuilder *>(userdata)->enterSpan(type, detail); }
int ContentBuilder::sLeaveSpan(MD_SPANTYPE type, void *detail, void *userdata)
{ return static_cast<ContentBuilder *>(userdata)->leaveSpan(type, detail); }
int ContentBuilder::sText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
{ return static_cast<ContentBuilder *>(userdata)->onText(type, text, size); }

// --- Block handlers ---

int ContentBuilder::enterBlock(MD_BLOCKTYPE type, void *detail)
{
    // Push source tracker for block types that produce content blocks
    if (type == MD_BLOCK_P || type == MD_BLOCK_H || type == MD_BLOCK_CODE
        || type == MD_BLOCK_TABLE || type == MD_BLOCK_UL || type == MD_BLOCK_OL
        || type == MD_BLOCK_HR) {
        m_blockTrackers.push(BlockTracker{});
    }

    switch (type) {
    case MD_BLOCK_DOC:
        break;

    case MD_BLOCK_P: {
        // Resolve paragraph format and text style
        Content::ParagraphFormat fmt;
        if (m_blockQuoteLevel > 0) {
            if (m_styleManager) {
                fmt = resolveParagraphFormat(QStringLiteral("BlockQuote"));
                m_currentStyle = resolveTextStyle(QStringLiteral("BlockQuote"));
            } else {
                fmt.leftMargin = 20.0 * m_blockQuoteLevel;
                m_currentStyle.italic = true;
                m_currentStyle.foreground = QColor(0x55, 0x55, 0x55);
            }
        } else if (!m_listStack.isEmpty()) {
            if (m_styleManager) {
                fmt = resolveParagraphFormat(QStringLiteral("ListItem"));
                m_currentStyle = resolveTextStyle(QStringLiteral("ListItem"));
            }
        } else {
            if (m_styleManager) {
                fmt = resolveParagraphFormat(QStringLiteral("BodyText"));
                m_currentStyle = resolveTextStyle(QStringLiteral("BodyText"));
            } else {
                fmt.spaceAfter = 6.0;
            }
        }

        // Place paragraph in the right container
        if (!m_listStack.isEmpty() && !m_listStack.top().items.isEmpty()) {
            auto &children = m_listStack.top().items.last().children;
            children.append(Content::Paragraph{fmt, {}});
            auto *p = std::get_if<Content::Paragraph>(&children.last());
            m_inlineStack.push(&p->inlines);
        } else if (!m_blockQuoteStack.isEmpty()) {
            m_blockQuoteStack.top().append(Content::Paragraph{fmt, {}});
            auto *p = std::get_if<Content::Paragraph>(&m_blockQuoteStack.top().last());
            m_inlineStack.push(&p->inlines);
        } else {
            m_doc.blocks.append(Content::Paragraph{fmt, {}});
            auto *p = std::get_if<Content::Paragraph>(&m_doc.blocks.last());
            m_inlineStack.push(&p->inlines);
        }
        break;
    }

    case MD_BLOCK_H: {
        auto *d = static_cast<MD_BLOCK_H_DETAIL *>(detail);
        int level = static_cast<int>(d->level);

        Content::Heading heading;
        heading.level = level;
        if (m_styleManager) {
            QString styleName = QStringLiteral("Heading%1").arg(level);
            heading.format = resolveParagraphFormat(styleName);
            heading.format.headingLevel = level;
            m_currentStyle = resolveTextStyle(styleName);
        } else {
            static const qreal spaceBefore[] = {0, 24, 20, 16, 12, 10, 8};
            static const qreal spaceAfter[] = {0, 12, 10, 8, 6, 4, 4};
            static const qreal sizes[] = {0, 28, 24, 20, 16, 14, 12};
            heading.format.spaceBefore = spaceBefore[level];
            heading.format.spaceAfter = spaceAfter[level];
            heading.format.headingLevel = level;
            m_currentStyle.fontFamily = QStringLiteral("Noto Sans");
            m_currentStyle.fontWeight = 700;
            m_currentStyle.fontSize = sizes[level];
            if (level == 6) m_currentStyle.italic = true;
        }

        if (!m_blockQuoteStack.isEmpty()) {
            m_blockQuoteStack.top().append(std::move(heading));
            auto *h = std::get_if<Content::Heading>(&m_blockQuoteStack.top().last());
            m_inlineStack.push(&h->inlines);
        } else {
            m_doc.blocks.append(std::move(heading));
            auto *h = std::get_if<Content::Heading>(&m_doc.blocks.last());
            m_inlineStack.push(&h->inlines);
        }
        break;
    }

    case MD_BLOCK_QUOTE:
        m_blockQuoteLevel++;
        m_blockQuoteStack.push(QList<Content::BlockNode>{});
        break;

    case MD_BLOCK_UL: {
        // Close parent item's implicit paragraph before nesting
        if (!m_listStack.isEmpty() && m_listStack.top().hasImplicitParagraph) {
            if (!m_inlineStack.isEmpty())
                m_inlineStack.pop();
            m_listStack.top().hasImplicitParagraph = false;
        }
        ListInfo info;
        info.type = Content::ListType::Unordered;
        info.startNumber = 1;
        m_listStack.push(info);
        break;
    }

    case MD_BLOCK_OL: {
        // Close parent item's implicit paragraph before nesting
        if (!m_listStack.isEmpty() && m_listStack.top().hasImplicitParagraph) {
            if (!m_inlineStack.isEmpty())
                m_inlineStack.pop();
            m_listStack.top().hasImplicitParagraph = false;
        }
        auto *d = static_cast<MD_BLOCK_OL_DETAIL *>(detail);
        ListInfo info;
        info.type = Content::ListType::Ordered;
        info.startNumber = static_cast<int>(d->start);
        m_listStack.push(info);
        break;
    }

    case MD_BLOCK_LI: {
        auto *d = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
        if (!m_listStack.isEmpty()) {
            // Close previous item's implicit paragraph
            if (m_listStack.top().hasImplicitParagraph) {
                if (!m_inlineStack.isEmpty())
                    m_inlineStack.pop();
                m_listStack.top().hasImplicitParagraph = false;
            }
            Content::ListItem item;
            item.isTask = d->is_task;
            item.taskChecked = (d->task_mark != ' ');
            m_listStack.top().items.append(item);
        }
        // Set list item text style for tight lists (no P block will set it)
        if (m_styleManager)
            m_currentStyle = resolveTextStyle(QStringLiteral("ListItem"));
        break;
    }

    case MD_BLOCK_CODE: {
        auto *d = static_cast<MD_BLOCK_CODE_DETAIL *>(detail);
        m_codeLanguage = extractAttribute(d->lang);
        m_codeFenced = (d->fence_char != 0);
        m_inCodeBlock = true;
        m_codeText.clear();
        break;
    }

    case MD_BLOCK_HR:
        addBlock(Content::HorizontalRule{12.0, 12.0});
        break;

    case MD_BLOCK_TABLE: {
        m_inTable = true;
        m_tableRows.clear();
        m_tableColumnAligns.clear();
        auto *d = static_cast<MD_BLOCK_TABLE_DETAIL *>(detail);
        m_tableColumnAligns.resize(static_cast<int>(d->col_count), Qt::AlignLeft);
        break;
    }

    case MD_BLOCK_THEAD:
        m_inTableHeader = true;
        break;

    case MD_BLOCK_TBODY:
        m_inTableHeader = false;
        break;

    case MD_BLOCK_TR:
        m_currentRowCells.clear();
        m_tableCol = 0;
        break;

    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        auto *d = static_cast<MD_BLOCK_TD_DETAIL *>(detail);
        Content::TableCell cell;
        cell.isHeader = (type == MD_BLOCK_TH);
        switch (d->align) {
        case MD_ALIGN_CENTER: cell.alignment = Qt::AlignCenter; break;
        case MD_ALIGN_RIGHT: cell.alignment = Qt::AlignRight; break;
        default: cell.alignment = Qt::AlignLeft; break;
        }
        if (m_tableCol < m_tableColumnAligns.size())
            m_tableColumnAligns[m_tableCol] = cell.alignment;

        // Resolve cell style
        if (m_styleManager) {
            TableStyle *ts = m_styleManager->tableStyle(QStringLiteral("Default"));
            if (ts) {
                if (cell.isHeader) {
                    cell.style = resolveTextStyle(ts->headerParagraphStyle());
                    cell.style.fontWeight = 700;
                    if (ts->hasHeaderBackground())
                        cell.background = ts->headerBackground();
                    else
                        cell.background = QColor(0xf0, 0xf0, 0xf0);
                    if (ts->hasHeaderForeground())
                        cell.style.foreground = ts->headerForeground();
                } else {
                    cell.style = resolveTextStyle(ts->bodyParagraphStyle());
                    // Body cell backgrounds handled by layout engine
                    // (applies bodyBackground + alternating row colors)
                }
            }
        } else if (cell.isHeader) {
            cell.background = QColor(0xf0, 0xf0, 0xf0);
            cell.style.fontWeight = 700;
        }

        m_currentStyle = cell.style.fontFamily.isEmpty() ? defaultTextStyle() : cell.style;
        m_currentRowCells.append(cell);
        m_inlineStack.push(&m_currentRowCells.last().inlines);
        break;
    }

    case MD_BLOCK_HTML:
        break;
    }

    return 0;
}

int ContentBuilder::leaveBlock(MD_BLOCKTYPE type, void * /*detail*/)
{
    switch (type) {
    case MD_BLOCK_P: {
        if (!m_inlineStack.isEmpty())
            m_inlineStack.pop();
        if (!m_blockTrackers.isEmpty()) {
            auto tracker = m_blockTrackers.pop();
            Content::SourceRange range;
            if (tracker.firstByteOffset >= 0) {
                range.startLine = byteOffsetToLine(tracker.firstByteOffset);
                range.endLine = byteOffsetToLine(tracker.lastByteEnd - 1);
            }
            if (!m_listStack.isEmpty() && !m_listStack.top().items.isEmpty()) {
                auto &children = m_listStack.top().items.last().children;
                if (!children.isEmpty()) {
                    auto *p = std::get_if<Content::Paragraph>(&children.last());
                    if (p) p->source = range;
                }
            } else if (!m_blockQuoteStack.isEmpty() && !m_blockQuoteStack.top().isEmpty()) {
                auto *p = std::get_if<Content::Paragraph>(&m_blockQuoteStack.top().last());
                if (p) p->source = range;
            } else if (!m_doc.blocks.isEmpty()) {
                auto *p = std::get_if<Content::Paragraph>(&m_doc.blocks.last());
                if (p) p->source = range;
            }
        }
        break;
    }

    case MD_BLOCK_H: {
        if (!m_inlineStack.isEmpty())
            m_inlineStack.pop();
        if (!m_blockTrackers.isEmpty()) {
            auto tracker = m_blockTrackers.pop();
            Content::SourceRange range;
            if (tracker.firstByteOffset >= 0) {
                range.startLine = byteOffsetToLine(tracker.firstByteOffset);
                range.endLine = byteOffsetToLine(tracker.lastByteEnd - 1);
            }
            if (!m_blockQuoteStack.isEmpty() && !m_blockQuoteStack.top().isEmpty()) {
                auto *h = std::get_if<Content::Heading>(&m_blockQuoteStack.top().last());
                if (h) h->source = range;
            } else if (!m_doc.blocks.isEmpty()) {
                auto *h = std::get_if<Content::Heading>(&m_doc.blocks.last());
                if (h) h->source = range;
            }
        }
        break;
    }

    case MD_BLOCK_QUOTE: {
        m_blockQuoteLevel--;
        QList<Content::BlockNode> children;
        if (!m_blockQuoteStack.isEmpty())
            children = m_blockQuoteStack.pop();

        Content::BlockQuote bq;
        bq.level = m_blockQuoteLevel + 1; // level of this blockquote (1-based)
        bq.children = std::move(children);
        if (m_styleManager)
            bq.format = resolveParagraphFormat(QStringLiteral("BlockQuote"));

        addBlock(std::move(bq));
        break;
    }

    case MD_BLOCK_LI:
        // Close implicit paragraph if open
        if (!m_listStack.isEmpty() && m_listStack.top().hasImplicitParagraph) {
            if (!m_inlineStack.isEmpty())
                m_inlineStack.pop();
            m_listStack.top().hasImplicitParagraph = false;
        }
        break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL: {
        Content::SourceRange range;
        if (!m_blockTrackers.isEmpty()) {
            auto tracker = m_blockTrackers.pop();
            if (tracker.firstByteOffset >= 0) {
                range.startLine = byteOffsetToLine(tracker.firstByteOffset);
                range.endLine = byteOffsetToLine(tracker.lastByteEnd - 1);
            }
        }
        if (!m_listStack.isEmpty()) {
            ListInfo info = m_listStack.pop();
            Content::List list;
            list.type = info.type;
            list.startNumber = info.startNumber;
            list.items = info.items;
            list.source = range;

            if (!m_listStack.isEmpty() && !m_listStack.top().items.isEmpty()) {
                // Nested list: add to parent list's current item
                m_listStack.top().items.last().children.append(std::move(list));
            } else {
                addBlock(std::move(list));
            }
        }
        break;
    }

    case MD_BLOCK_CODE: {
        m_inCodeBlock = false;
        Content::CodeBlock cb;
        cb.language = m_codeLanguage;
        cb.isFenced = m_codeFenced;
        cb.code = m_codeText;
        if (m_styleManager) {
            cb.style = resolveTextStyle(QStringLiteral("CodeBlock"));
            ParagraphStyle ps = m_styleManager->resolvedParagraphStyle(QStringLiteral("CodeBlock"));
            cb.lineHeightPercent = ps.lineHeightPercent();
            if (ps.hasBackground())
                cb.background = ps.background();
        } else {
            cb.style.fontFamily = QStringLiteral("JetBrains Mono");
            cb.style.fontSize = 10.0;
        }
        if (!m_blockTrackers.isEmpty()) {
            auto tracker = m_blockTrackers.pop();
            if (tracker.firstByteOffset >= 0) {
                cb.source.startLine = byteOffsetToLine(tracker.firstByteOffset);
                cb.source.endLine = byteOffsetToLine(tracker.lastByteEnd - 1);
            }
        }
        addBlock(std::move(cb));
        m_codeLanguage.clear();
        m_codeText.clear();
        break;
    }

    case MD_BLOCK_TABLE: {
        m_inTable = false;
        Content::Table table;
        table.rows = m_tableRows;
        table.headerRowCount = 0;
        for (const auto &row : m_tableRows) {
            if (!row.cells.isEmpty() && row.cells.first().isHeader)
                table.headerRowCount++;
            else
                break;
        }
        // Apply table style
        if (m_styleManager) {
            TableStyle *ts = m_styleManager->tableStyle(QStringLiteral("Default"));
            if (ts) {
                if (ts->hasHeaderBackground()) table.headerBackground = ts->headerBackground();
                if (ts->hasHeaderForeground()) table.headerForeground = ts->headerForeground();
                if (ts->hasBodyBackground()) table.bodyBackground = ts->bodyBackground();
                if (ts->hasAlternateRowColor()) table.alternateRowColor = ts->alternateRowColor();
                table.cellPadding = ts->cellPadding().top();
                if (ts->hasOuterBorder()) {
                    table.borderWidth = ts->outerBorder().width;
                    table.borderColor = ts->outerBorder().color;
                }
                if (ts->hasInnerBorder()) {
                    table.innerBorderWidth = ts->innerBorder().width;
                    table.innerBorderColor = ts->innerBorder().color;
                }
                if (ts->hasHeaderBottomBorder()) {
                    table.headerBottomBorderWidth = ts->headerBottomBorder().width;
                    table.headerBottomBorderColor = ts->headerBottomBorder().color;
                }
            }
        }
        if (!m_blockTrackers.isEmpty()) {
            auto tracker = m_blockTrackers.pop();
            if (tracker.firstByteOffset >= 0) {
                table.source.startLine = byteOffsetToLine(tracker.firstByteOffset);
                table.source.endLine = byteOffsetToLine(tracker.lastByteEnd - 1);
            }
        }
        addBlock(std::move(table));
        m_tableRows.clear();
        break;
    }

    case MD_BLOCK_TR: {
        Content::TableRow row;
        row.cells = m_currentRowCells;
        m_tableRows.append(row);
        m_currentRowCells.clear();
        break;
    }

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        if (!m_inlineStack.isEmpty())
            m_inlineStack.pop();
        m_tableCol++;
        break;

    case MD_BLOCK_HR:
        if (!m_blockTrackers.isEmpty()) {
            auto tracker = m_blockTrackers.pop();
            if (tracker.firstByteOffset >= 0) {
                Content::SourceRange range;
                range.startLine = byteOffsetToLine(tracker.firstByteOffset);
                range.endLine = byteOffsetToLine(tracker.lastByteEnd - 1);
                // Find the HR in whatever container addBlock placed it
                auto setHrSource = [&](QList<Content::BlockNode> &blocks) {
                    if (!blocks.isEmpty()) {
                        auto *hr = std::get_if<Content::HorizontalRule>(&blocks.last());
                        if (hr) hr->source = range;
                    }
                };
                if (!m_blockQuoteStack.isEmpty())
                    setHrSource(m_blockQuoteStack.top());
                else if (!m_listStack.isEmpty() && !m_listStack.top().items.isEmpty())
                    setHrSource(m_listStack.top().items.last().children);
                else
                    setHrSource(m_doc.blocks);
            }
        }
        break;

    default:
        break;
    }
    return 0;
}

// --- Span handlers ---

int ContentBuilder::enterSpan(MD_SPANTYPE type, void *detail)
{
    switch (type) {
    case MD_SPAN_EM:
        m_styleStack.push(m_currentStyle);
        m_currentStyle.italic = true;
        break;

    case MD_SPAN_STRONG:
        m_styleStack.push(m_currentStyle);
        m_currentStyle.fontWeight = 700;
        break;

    case MD_SPAN_CODE:
        m_styleStack.push(m_currentStyle);
        if (m_styleManager)
            m_currentStyle = resolveCharStyle(QStringLiteral("InlineCode"));
        else {
            m_currentStyle.fontFamily = QStringLiteral("JetBrains Mono");
            m_currentStyle.fontSize = 10.0;
            m_currentStyle.foreground = QColor(0xc7, 0x25, 0x4e);
            m_currentStyle.background = QColor(0xf0, 0xf0, 0xf0);
        }
        break;

    case MD_SPAN_A: {
        auto *d = static_cast<MD_SPAN_A_DETAIL *>(detail);
        m_styleStack.push(m_currentStyle);
        if (m_styleManager)
            m_currentStyle = resolveCharStyle(QStringLiteral("Link"));
        else {
            m_currentStyle.foreground = QColor(0x03, 0x66, 0xd6);
            m_currentStyle.underline = true;
        }
        m_linkHref = extractAttribute(d->href);
        m_currentStyle.linkHref = m_linkHref;
        break;
    }

    case MD_SPAN_IMG: {
        auto *d = static_cast<MD_SPAN_IMG_DETAIL *>(detail);
        m_imageSrc = extractAttribute(d->src);
        m_imageTitle = extractAttribute(d->title);
        m_collectingAltText = true;
        m_altText.clear();
        break;
    }

    case MD_SPAN_DEL:
        m_styleStack.push(m_currentStyle);
        m_currentStyle.strikethrough = true;
        break;

    case MD_SPAN_U:
        m_styleStack.push(m_currentStyle);
        m_currentStyle.underline = true;
        break;

    case MD_SPAN_WIKILINK:
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        m_styleStack.push(m_currentStyle);
        break;
    }
    return 0;
}

int ContentBuilder::leaveSpan(MD_SPANTYPE type, void * /*detail*/)
{
    switch (type) {
    case MD_SPAN_IMG: {
        m_collectingAltText = false;
        QString src = m_imageSrc;
        QString resolved;
        if (QFileInfo(src).isRelative() && !m_basePath.isEmpty())
            resolved = QDir(m_basePath).filePath(src);
        else
            resolved = src;

        Content::InlineImage img;
        img.src = src;
        img.altText = m_altText;
        QImage loadedImg(resolved);
        if (!loadedImg.isNull()) {
            img.width = loadedImg.width();
            img.height = loadedImg.height();
            // Store as PNG data
            QBuffer buf(&img.resolvedImageData);
            buf.open(QIODevice::WriteOnly);
            loadedImg.save(&buf, "PNG");
        }
        appendInlineNode(std::move(img));
        m_imageSrc.clear();
        m_imageTitle.clear();
        m_altText.clear();
        break;
    }

    case MD_SPAN_A:
        if (!m_styleStack.isEmpty())
            m_currentStyle = m_styleStack.pop();
        m_linkHref.clear();
        m_currentStyle.linkHref.clear();
        break;

    default:
        if (!m_styleStack.isEmpty()) {
            m_currentStyle = m_styleStack.pop();
            // Preserve linkHref if we're still inside a link span
            if (!m_linkHref.isEmpty())
                m_currentStyle.linkHref = m_linkHref;
        }
        break;
    }
    return 0;
}

// --- Text handler ---

int ContentBuilder::onText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size)
{
    // Update all active block trackers with source position
    if (m_bufferStart) {
        int offset = static_cast<int>(text - m_bufferStart);
        int end = offset + static_cast<int>(size);
        for (auto &tracker : m_blockTrackers) {
            if (tracker.firstByteOffset < 0)
                tracker.firstByteOffset = offset;
            tracker.lastByteEnd = end;
        }
    }

    QString str = QString::fromUtf8(text, static_cast<int>(size));

    if (m_collectingAltText) {
        m_altText.append(str);
        return 0;
    }

    switch (type) {
    case MD_TEXT_NORMAL: {
        // Check for footnote references
        if (!m_footnotes.isEmpty()) {
            static const QRegularExpression fnRefRx(QStringLiteral(R"(\[\^([^\]]+)\])"));
            int lastEnd = 0;
            auto it = fnRefRx.globalMatch(str);
            bool found = false;
            while (it.hasNext()) {
                auto match = it.next();
                if (match.capturedStart() > lastEnd) {
                    QString seg = str.mid(lastEnd, match.capturedStart() - lastEnd);
                    if (!m_inCodeBlock)
                        seg = processTypography(seg);
                    appendInlineNode(Content::TextRun{seg, m_currentStyle});
                }
                QString label = match.captured(1);
                int fnIndex = -1;
                for (int i = 0; i < m_footnotes.size(); ++i) {
                    if (m_footnotes[i].label == label) {
                        fnIndex = i;
                        break;
                    }
                }
                if (fnIndex >= 0) {
                    int number = m_footnoteStyle.startNumber + fnIndex;
                    Content::FootnoteRef ref;
                    ref.index = fnIndex;
                    ref.label = m_footnoteStyle.formatNumber(number);
                    ref.style = m_currentStyle;
                    ref.style.fontSize = 8.0;
                    ref.style.foreground = QColor(0x03, 0x66, 0xd6);
                    ref.style.superscript = m_footnoteStyle.superscriptRef;
                    appendInlineNode(std::move(ref));
                } else {
                    appendInlineNode(Content::TextRun{match.captured(0), m_currentStyle});
                }
                lastEnd = match.capturedEnd();
                found = true;
            }
            if (found) {
                if (lastEnd < str.size()) {
                    QString tail = str.mid(lastEnd);
                    if (!m_inCodeBlock)
                        tail = processTypography(tail);
                    appendInlineNode(Content::TextRun{tail, m_currentStyle});
                }
            } else {
                if (!m_inCodeBlock)
                    str = processTypography(str);
                appendInlineNode(Content::TextRun{str, m_currentStyle});
            }
        } else {
            if (!m_inCodeBlock)
                str = processTypography(str);
            appendInlineNode(Content::TextRun{str, m_currentStyle});
        }
        break;
    }

    case MD_TEXT_CODE:
        if (m_inCodeBlock) {
            m_codeText.append(str);
        } else {
            appendInlineNode(Content::InlineCode{str, m_currentStyle});
        }
        break;

    case MD_TEXT_BR:
        appendInlineNode(Content::HardBreak{});
        break;

    case MD_TEXT_SOFTBR:
        appendInlineNode(Content::SoftBreak{});
        break;

    case MD_TEXT_ENTITY: {
        QString decoded = resolveEntity(str);
        appendInlineNode(Content::TextRun{decoded, m_currentStyle});
        break;
    }

    case MD_TEXT_NULLCHAR:
        appendInlineNode(Content::TextRun{QString(QChar(0xFFFD)), m_currentStyle});
        break;

    case MD_TEXT_HTML:
        break;

    case MD_TEXT_LATEXMATH:
        appendInlineNode(Content::TextRun{str, m_currentStyle});
        break;
    }
    return 0;
}

// --- Source tracking ---

int ContentBuilder::byteOffsetToLine(int offset) const
{
    // Binary search: find 1-based line number containing this byte offset
    auto it = std::upper_bound(m_lineStartOffsets.begin(), m_lineStartOffsets.end(), offset);
    return static_cast<int>(it - m_lineStartOffsets.begin()); // 1-based
}

// --- Helpers ---

QString ContentBuilder::extractAttribute(const MD_ATTRIBUTE &attr)
{
    if (!attr.text || attr.size == 0)
        return {};
    return QString::fromUtf8(attr.text, static_cast<int>(attr.size));
}

QString ContentBuilder::resolveEntity(const QString &entity)
{
    static const QHash<QString, QString> entities = {
        {QStringLiteral("&amp;"),    QStringLiteral("&")},
        {QStringLiteral("&lt;"),     QStringLiteral("<")},
        {QStringLiteral("&gt;"),     QStringLiteral(">")},
        {QStringLiteral("&quot;"),   QStringLiteral("\"")},
        {QStringLiteral("&apos;"),   QStringLiteral("'")},
        {QStringLiteral("&nbsp;"),   QString(QChar(0x00A0))},
        {QStringLiteral("&mdash;"),  QString(QChar(0x2014))},
        {QStringLiteral("&ndash;"),  QString(QChar(0x2013))},
        {QStringLiteral("&lsquo;"),  QString(QChar(0x2018))},
        {QStringLiteral("&rsquo;"),  QString(QChar(0x2019))},
        {QStringLiteral("&ldquo;"),  QString(QChar(0x201C))},
        {QStringLiteral("&rdquo;"),  QString(QChar(0x201D))},
        {QStringLiteral("&hellip;"), QString(QChar(0x2026))},
        {QStringLiteral("&copy;"),   QString(QChar(0x00A9))},
        {QStringLiteral("&reg;"),    QString(QChar(0x00AE))},
        {QStringLiteral("&trade;"),  QString(QChar(0x2122))},
        {QStringLiteral("&deg;"),    QString(QChar(0x00B0))},
        {QStringLiteral("&times;"),  QString(QChar(0x00D7))},
        {QStringLiteral("&divide;"), QString(QChar(0x00F7))},
    };
    auto it = entities.constFind(entity);
    if (it != entities.constEnd())
        return it.value();
    if (entity.startsWith(QLatin1String("&#"))) {
        QString num = entity.mid(2, entity.size() - 3);
        bool ok;
        uint code;
        if (num.startsWith(QLatin1Char('x'), Qt::CaseInsensitive))
            code = num.mid(1).toUInt(&ok, 16);
        else
            code = num.toUInt(&ok, 10);
        if (ok && code > 0)
            return QString(QChar(code));
    }
    return entity;
}
