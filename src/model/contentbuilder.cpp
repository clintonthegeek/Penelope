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
#include "frontmatterparser.h"
#include "entitytable.h"
#include "smartpunctuation.h"

#include <algorithm>

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include "penelopesettings.h"
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
    CharacterStyle cs = m_styleManager->characterStyleOverrides(charStyleName);
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

QList<Content::BlockNode> &ContentBuilder::currentBlocks()
{
    if (!m_containers.isEmpty()) {
        if (m_containers.top() == MD_BLOCK_QUOTE) return m_blockQuoteStack.top();
        if (!m_listStack.isEmpty() && !m_listStack.top().items.isEmpty())
            return m_listStack.top().items.last().children;
    }
    return m_doc.blocks;
}

void ContentBuilder::addBlock(Content::BlockNode block)
{
    std::visit([&](auto &b) { b.blockId = m_nextBlockId++; }, block);
    currentBlocks().append(std::move(block));
}

void ContentBuilder::closeImplicitParagraph()
{
    if (m_listStack.isEmpty() || !m_listStack.top().hasImplicitParagraph) return;
    auto &children = m_listStack.top().items.last().children;
    if (!children.isEmpty() && !m_blockTrackers.isEmpty()) {
        auto *para = std::get_if<Content::Paragraph>(&children.last());
        const auto &tracker = m_blockTrackers.top();
        if (para && tracker.firstByteOffset >= 0)
            para->source = {byteOffsetToLine(tracker.firstByteOffset), byteOffsetToLine(tracker.lastByteEnd - 1)};
    }
    if (!m_inlineStack.isEmpty()) m_inlineStack.pop();
    m_listStack.top().hasImplicitParagraph = false;
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
            children.append(Content::Paragraph{0, fmt, {}});
            auto *p = std::get_if<Content::Paragraph>(&children.last());
            if (p) p->blockId = m_nextBlockId++;
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
    if (m_smartPunctuation)
        result = applySmartPunctuation(result);
    if (m_shortWords)
        result = m_shortWords->process(result);
    if (m_hyphenator && m_hyphenator->isLoaded())
        result = m_hyphenator->hyphenateText(result);
    return result;
}

// --- Build entry point ---

Content::Document ContentBuilder::build(const QString &markdownText)
{
    resetState();

    // Front matter is metadata, not body. Blanking its lines before the
    // footnote scan keeps `title: [^a]`-style text out of the note parser too.
    FrontMatterParser fmParser;
    QString body = fmParser.process(markdownText);
    m_doc.hasFrontmatter = fmParser.frontMatter().present;
    m_doc.frontmatterRaw = fmParser.frontMatter().raw;

    FootnoteParser fnParser;
    const QString processed = fnParser.process(body);
    for (const auto &fn : fnParser.footnotes())
        m_footnotes.append({fn.label, fn.content});
    m_footnoteRefSites.resize(m_footnotes.size());

    m_doc.footnoteStyle = m_footnoteStyle;
    parseIntoDocument(processed);
    appendFootnoteSection();
    return m_doc;
}

void ContentBuilder::resetState()
{
    m_doc = Content::Document{};
    m_nextBlockId = 1;
    m_containers.clear();
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
    m_linkSpanDepth = 0;
    m_inMath = false;
    m_mathDisplay = false;
    m_footnotes.clear();
    m_footnoteRefSites.clear();

    if (m_styleManager)
        m_currentStyle = resolveTextStyle(QStringLiteral("Default Paragraph Style"));
    else
        m_currentStyle = defaultTextStyle();
}

// Parses text that already passed the pre-passes. The line table, block
// trackers and MD4C callbacks all address this text.
void ContentBuilder::parseIntoDocument(const QString &text)
{
    m_processedMarkdown = text;

    const QByteArray utf8 = text.toUtf8();
    m_lineStartOffsets.clear();
    m_lineStartOffsets.append(0); // line 1 starts at byte 0
    for (int i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '\n')
            m_lineStartOffsets.append(i + 1);
    }
    m_bufferStart = utf8.constData();
    m_blockTrackers.clear();

    MD_PARSER parser = {};
    parser.abi_version = 0;
    // MD_FLAG_UNDERLINE would make `_x_` underline *and* disable `_` emphasis.
    // CommonMark, and Markoff's parser, treat `_x_` as emphasis; underline is a
    // style-system property that nothing in the dialect produces.
    parser.flags = MD_DIALECT_GITHUB
                 | MD_FLAG_WIKILINKS | MD_FLAG_LATEXMATHSPANS;
    parser.enter_block = &ContentBuilder::sEnterBlock;
    parser.leave_block = &ContentBuilder::sLeaveBlock;
    parser.enter_span = &ContentBuilder::sEnterSpan;
    parser.leave_span = &ContentBuilder::sLeaveSpan;
    parser.text = &ContentBuilder::sText;

    md_parse(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), &parser, this);
}

// Note bodies are their own Markdown fragment. A second builder shares this
// one's configuration and continues its block-id counter, so ids stay unique
// across the note sections.
QList<Content::BlockNode> ContentBuilder::buildFragment(const QString &markdownText)
{
    ContentBuilder fragment;
    fragment.setStyleManager(m_styleManager);
    fragment.setHyphenator(m_hyphenator);
    fragment.setShortWords(m_shortWords);
    fragment.setFootnoteStyle(m_footnoteStyle);
    fragment.setBasePath(m_basePath);
    fragment.setRenderImages(m_renderImages ? *m_renderImages
                                            : PenelopeSettings::self()->renderImages());
    fragment.setCancellationCheck(m_cancelled);
    fragment.resetState();
    fragment.m_nextBlockId = m_nextBlockId;
    fragment.parseIntoDocument(markdownText);

    m_nextBlockId = fragment.m_nextBlockId;
    return fragment.m_doc.blocks;
}

void ContentBuilder::appendFootnoteSection()
{
    if (m_footnotes.isEmpty())
        return;

    Content::FootnoteSection section;
    section.showSeparator = m_footnoteStyle.showSeparator;
    section.separatorLength = m_footnoteStyle.separatorLength;
    section.separatorWidth = m_footnoteStyle.separatorWidth;

    for (int i = 0; i < m_footnotes.size(); ++i) {
        Content::Footnote fn;
        fn.index = i;

        Content::TextStyle numStyle = m_currentStyle;
        numStyle.fontSize = 8.0;
        numStyle.foreground = QColor(0x03, 0x66, 0xd6);
        numStyle.superscript = m_footnoteStyle.superscriptNote;
        fn.numberStyle = numStyle;

        Content::TextStyle textStyle = m_currentStyle;
        textStyle.fontSize = 9.0;
        textStyle.foreground = QColor(0x55, 0x55, 0x55);
        fn.textStyle = textStyle;

        fn.blocks = buildFragment(m_footnotes[i].text);
        fn.references = m_footnoteRefSites.value(i);
        section.footnotes.append(fn);
    }

    section.blockId = m_nextBlockId++;
    m_doc.blocks.append(section);
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
    if (m_cancelled && m_cancelled()) return 1;
    if (type == MD_BLOCK_UL || type == MD_BLOCK_OL || type == MD_BLOCK_QUOTE
        || type == MD_BLOCK_CODE || type == MD_BLOCK_TABLE || type == MD_BLOCK_H || type == MD_BLOCK_HR)
        closeImplicitParagraph();
    // Push source tracker for block types that produce content blocks
    if (type == MD_BLOCK_LI || type == MD_BLOCK_P || type == MD_BLOCK_H || type == MD_BLOCK_CODE
        || type == MD_BLOCK_TABLE || type == MD_BLOCK_UL || type == MD_BLOCK_OL
        || type == MD_BLOCK_HR || type == MD_BLOCK_QUOTE || type == MD_BLOCK_HTML) {
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

        auto &children = currentBlocks();
        children.append(Content::Paragraph{0, fmt, {}});
        auto *p = std::get_if<Content::Paragraph>(&children.last());
        if (p) p->blockId = m_nextBlockId++;
        m_inlineStack.push(&p->inlines);
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

        auto &children = currentBlocks();
        children.append(std::move(heading));
        auto *h = std::get_if<Content::Heading>(&children.last());
        h->blockId = m_nextBlockId++;
        m_inlineStack.push(&h->inlines);
        break;
    }

    case MD_BLOCK_QUOTE:
        m_containers.push(type);
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
        m_containers.push(type);
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
        m_containers.push(type);
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
        addBlock(Content::HorizontalRule{0, 12.0, 12.0});
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

    case MD_BLOCK_HTML: {
        // Keep the block: dropping it also dropped its inner text.
        Content::ParagraphFormat fmt;
        if (m_styleManager)
            fmt = resolveParagraphFormat(QStringLiteral("BodyText"));
        else
            fmt.spaceAfter = 6.0;
        auto &children = currentBlocks();
        children.append(Content::Paragraph{0, fmt, {}});
        auto *paragraph = std::get_if<Content::Paragraph>(&children.last());
        m_inlineStack.push(&paragraph->inlines);
        break;
    }
    }

    return 0;
}

int ContentBuilder::leaveBlock(MD_BLOCKTYPE type, void * /*detail*/)
{
    if (m_cancelled && m_cancelled()) return 1;
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
            auto &children = currentBlocks();
            if (!children.isEmpty()) {
                auto *node = std::get_if<Content::Paragraph>(&children.last());
                if (node) node->source = range;
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
            auto &children = currentBlocks();
            if (!children.isEmpty()) {
                auto *node = std::get_if<Content::Heading>(&children.last());
                if (node) node->source = range;
            }
        }
        break;
    }

    case MD_BLOCK_QUOTE: {
        m_containers.pop();
        m_blockQuoteLevel--;
        QList<Content::BlockNode> children;
        if (!m_blockQuoteStack.isEmpty())
            children = m_blockQuoteStack.pop();

        Content::BlockQuote bq;
        if (!m_blockTrackers.isEmpty()) {
            const auto tracker = m_blockTrackers.pop();
            if (tracker.firstByteOffset >= 0) {
                bq.source.startLine = byteOffsetToLine(tracker.firstByteOffset);
                bq.source.endLine = byteOffsetToLine(tracker.lastByteEnd - 1);
            }
        }
        bq.level = m_blockQuoteLevel + 1; // level of this blockquote (1-based)
        bq.children = std::move(children);
        if (m_styleManager)
            bq.format = resolveParagraphFormat(QStringLiteral("BlockQuote"));

        addBlock(std::move(bq));
        break;
    }

    case MD_BLOCK_LI:
        closeImplicitParagraph();
        if (!m_blockTrackers.isEmpty()) m_blockTrackers.pop();
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

            m_containers.pop();
            addBlock(std::move(list));
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

    case MD_BLOCK_HTML: {
        if (!m_inlineStack.isEmpty())
            m_inlineStack.pop();
        if (!m_blockTrackers.isEmpty()) {
            auto tracker = m_blockTrackers.pop();
            Content::SourceRange range;
            if (tracker.firstByteOffset >= 0) {
                range.startLine = byteOffsetToLine(tracker.firstByteOffset);
                range.endLine = byteOffsetToLine(tracker.lastByteEnd - 1);
            }
            auto &children = currentBlocks();
            if (!children.isEmpty()) {
                if (auto *node = std::get_if<Content::Paragraph>(&children.last()))
                    node->source = range;
            }
        }
        break;
    }

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
                setHrSource(currentBlocks());
            }
        }
        break;

    default:
        break;
    }
    return 0;
}

// --- Span handlers ---

// Where a link points, in the spirit of Markoff's LinkKind.
static Content::LinkKind classifyLinkHref(const QString &href)
{
    if (href.startsWith(QLatin1Char('#')))
        return Content::LinkKind::Anchor;
    if (href.startsWith(QLatin1String("mailto:"))
        || href.contains(QLatin1String("://"))
        || href.startsWith(QLatin1String("www.")))
        return Content::LinkKind::External;
    return Content::LinkKind::File;
}

int ContentBuilder::enterSpan(MD_SPANTYPE type, void *detail)
{
    if (m_cancelled && m_cancelled()) return 1;
    switch (type) {
    case MD_SPAN_EM:
        m_styleStack.push(m_currentStyle);
        if (m_styleManager && m_styleManager->characterStyle(QStringLiteral("Emphasis")))
            m_currentStyle = resolveCharStyle(QStringLiteral("Emphasis"));
        else
            m_currentStyle.italic = true;
        break;

    case MD_SPAN_STRONG:
        m_styleStack.push(m_currentStyle);
        if (m_styleManager && m_styleManager->characterStyle(QStringLiteral("Strong")))
            m_currentStyle = resolveCharStyle(QStringLiteral("Strong"));
        else
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
        m_currentStyle.linkTitle = extractAttribute(d->title);
        m_currentStyle.linkKind = classifyLinkHref(m_linkHref);
        ++m_linkSpanDepth;
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
        if (m_styleManager && m_styleManager->characterStyle(QStringLiteral("Strikethrough")))
            m_currentStyle = resolveCharStyle(QStringLiteral("Strikethrough"));
        else
            m_currentStyle.strikethrough = true;
        break;

    case MD_SPAN_U:
        m_styleStack.push(m_currentStyle);
        m_currentStyle.underline = true;
        break;

    case MD_SPAN_WIKILINK: {
        auto *d = static_cast<MD_SPAN_WIKILINK_DETAIL *>(detail);
        m_styleStack.push(m_currentStyle);
        m_currentStyle.linkHref = extractAttribute(d->target);
        m_currentStyle.linkKind = Content::LinkKind::WikiLink;
        ++m_linkSpanDepth;
        break;
    }

    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        m_styleStack.push(m_currentStyle);
        m_inMath = true;
        m_mathDisplay = (type == MD_SPAN_LATEXMATH_DISPLAY);
        break;
    }
    return 0;
}

int ContentBuilder::leaveSpan(MD_SPANTYPE type, void * /*detail*/)
{
    if (m_cancelled && m_cancelled()) return 1;
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
        img.title = m_imageTitle;
        if (!(m_renderImages ? *m_renderImages : PenelopeSettings::self()->renderImages())) {
            appendInlineNode(Content::TextRun{m_altText, m_currentStyle});
            m_imageSrc.clear(); m_imageTitle.clear(); m_altText.clear();
            break;
        }
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
        m_currentStyle.linkTitle.clear();
        m_currentStyle.linkKind = Content::LinkKind::None;
        if (m_linkSpanDepth > 0) --m_linkSpanDepth;
        break;

    case MD_SPAN_WIKILINK:
        if (!m_styleStack.isEmpty())
            m_currentStyle = m_styleStack.pop();
        if (m_linkSpanDepth > 0) --m_linkSpanDepth;
        break;

    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        if (!m_styleStack.isEmpty())
            m_currentStyle = m_styleStack.pop();
        m_inMath = false;
        m_mathDisplay = false;
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
    if (m_cancelled && m_cancelled()) return 1;
    const int textOffset = m_bufferStart ? static_cast<int>(text - m_bufferStart) : -1;
    // Update all active block trackers with source position
    if (m_bufferStart) {
        const int offset = textOffset;
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
    case MD_TEXT_NORMAL:
        emitNormalText(str, textOffset);
        break;

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
        // Literal HTML: no typography pass and no tag scan, the characters are
        // the content.
        appendInlineNode(Content::TextRun{str, m_currentStyle});
        break;

    case MD_TEXT_LATEXMATH: {
        Content::Math math;
        math.source = str;
        math.display = m_mathDisplay;
        math.style = m_currentStyle;
        appendInlineNode(std::move(math));
        break;
    }
    }
    return 0;
}


// Footnote references and Obsidian embeds are not MD4C spans, so a normal text
// run is split for them; #tags are split out of what remains.
void ContentBuilder::emitNormalText(const QString &text, int textByteOffset)
{
    static const QRegularExpression specials(
        QStringLiteral(R"((!\[\[[^\]]*\]\])|(\[\^[^\]]+\]))"));
    int last = 0;
    auto it = specials.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        if (match.capturedStart() > last)
            emitPlainText(text.mid(last, match.capturedStart() - last));
        // The match offset is a character index; the line table is byte based.
        const int byteOffset = textByteOffset < 0
            ? -1
            : textByteOffset + text.left(match.capturedStart()).toUtf8().size();
        if (!match.captured(1).isEmpty())
            emitEmbed(match.captured(1));
        else
            emitFootnoteRef(match.captured(2), byteOffset);
        last = match.capturedEnd();
    }
    if (last < text.size())
        emitPlainText(text.mid(last));
}

void ContentBuilder::emitPlainText(const QString &text)
{
    if (text.isEmpty())
        return;
    if (!m_inCodeBlock && m_linkSpanDepth == 0) {
        static const QRegularExpression tag(
            QStringLiteral(R"((^|[\s(\[])(#[\p{L}\p{N}_][\p{L}\p{N}_/-]*))"),
            QRegularExpression::UseUnicodePropertiesOption);
        int consumed = 0;
        auto it = tag.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            const int start = match.capturedStart(2);
            if (start > consumed)
                appendInlineNode(Content::TextRun{
                    processTypography(text.mid(consumed, start - consumed)), m_currentStyle});
            Content::TextStyle style = m_currentStyle;
            style.linkHref = match.captured(2);
            style.linkKind = Content::LinkKind::Tag;
            appendInlineNode(Content::TextRun{match.captured(2), style});
            consumed = match.capturedEnd(2);
        }
        if (consumed > 0) {
            if (consumed < text.size())
                appendInlineNode(Content::TextRun{
                    processTypography(text.mid(consumed)), m_currentStyle});
            return;
        }
    }
    appendInlineNode(Content::TextRun{
        m_inCodeBlock ? text : processTypography(text), m_currentStyle});
}

void ContentBuilder::emitFootnoteRef(const QString &reference, int byteOffset)
{
    const QString label = reference.mid(2, reference.size() - 3);
    int fnIndex = -1;
    for (int i = 0; i < m_footnotes.size(); ++i) {
        if (m_footnotes[i].label == label) {
            fnIndex = i;
            break;
        }
    }
    if (fnIndex < 0) {
        // A reference with no definition stays literal, as Markoff leaves it.
        appendInlineNode(Content::TextRun{reference, m_currentStyle});
        return;
    }
    bool isRepeat = false;
    if (fnIndex < m_footnoteRefSites.size()) {
        Content::FootnoteRefSite site;
        site.sourceLine = byteOffset >= 0 ? byteOffsetToLine(byteOffset) : 0;
        site.isRepeat = !m_footnoteRefSites.at(fnIndex).isEmpty();
        isRepeat = site.isRepeat;
        m_footnoteRefSites[fnIndex].append(site);
    }
    Content::FootnoteRef ref;
    ref.index = fnIndex;
    ref.isRepeat = isRepeat;
    ref.style = m_currentStyle;
    ref.style.fontSize = 8.0;
    ref.style.foreground = QColor(0x03, 0x66, 0xd6);
    ref.style.superscript = m_footnoteStyle.superscriptRef;
    appendInlineNode(std::move(ref));
}

void ContentBuilder::emitEmbed(const QString &embed)
{
    // ![[Target#Section|alias]]
    const QString body = embed.mid(3, embed.size() - 5);
    QString target = body;
    QString display = body;
    const int pipe = body.lastIndexOf(QLatin1Char('|'));
    if (pipe >= 0) {
        target = body.left(pipe);
        display = body.mid(pipe + 1);
    }
    target = target.trimmed();
    if (display.trimmed().isEmpty())
        display = target;
    Content::TextStyle style = m_currentStyle;
    style.linkHref = target;
    style.linkKind = Content::LinkKind::Embed;
    appendInlineNode(Content::TextRun{display, style});
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
    if (entity.startsWith(QLatin1String("&#"))) {
        QString num = entity.mid(2, entity.size() - 3);
        bool ok;
        uint code;
        if (num.startsWith(QLatin1Char('x'), Qt::CaseInsensitive))
            code = num.mid(1).toUInt(&ok, 16);
        else
            code = num.toUInt(&ok, 10);
        if (ok && code > 0 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF)) {
            const char32_t cp = code;
            return QString::fromUcs4(&cp, 1);
        }
        return entity;
    }
    // Named references: MD4C only matches the terminated spelling.
    if (entity.size() > 2 && entity.startsWith(QLatin1Char('&'))
        && entity.endsWith(QLatin1Char(';'))) {
        const QString decoded = decodeNamedEntity(entity.mid(1, entity.size() - 2));
        if (!decoded.isNull())
            return decoded;
    }
    return entity;
}
