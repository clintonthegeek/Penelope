#include "documentbuilder.h"
#include "stylemanager.h"
#include "paragraphstyle.h"
#include "characterstyle.h"
#include "tablestyle.h"
#include "hyphenator.h"
#include "shortwords.h"
#include "footnoteparser.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFont>
#include <QImage>
#include "penelopesettings.h"
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextFrame>
#include <QTextFrameFormat>
#include <QTextImageFormat>
#include <QTextLength>
#include <QTextListFormat>
#include <QTextTableFormat>
#include <QTextTableCellFormat>
#include <QUrl>

DocumentBuilder::DocumentBuilder(QTextDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_cursor(document)
{
}

bool DocumentBuilder::build(const QString &markdownText)
{
    m_document->clear();
    m_cursor = QTextCursor(m_document);
    m_charFormatStack.clear();
    m_listStack.clear();
    m_listItemBlockReady = false;
    m_currentTable = nullptr;
    m_tableRow = 0;
    m_tableCol = 0;
    m_blockQuoteLevel = 0;
    m_isFirstBlock = true;
    m_inCodeBlock = false;
    m_codeFrame = nullptr;
    m_inTableHeader = false;
    m_collectingAltText = false;
    m_altText.clear();
    m_codeLanguage.clear();
    m_footnotes.clear();
    m_footnoteCounter = 0;

    // Extract footnotes before parsing using the full footnote parser
    FootnoteParser fnParser;
    QString processedMarkdown = fnParser.process(markdownText);
    const auto &parsedFootnotes = fnParser.footnotes();
    for (const auto &fn : parsedFootnotes) {
        Footnote footnote;
        footnote.label = fn.label;
        footnote.text = fn.content;
        m_footnotes.append(footnote);
    }

    // Set the document default font from the resolved root paragraph style.
    // This ensures any text without explicit formatting matches the theme.
    if (m_styleManager) {
        ParagraphStyle rootStyle = m_styleManager->resolvedParagraphStyle(
            QStringLiteral("Default Paragraph Style"));
        QFont defaultFont(rootStyle.fontFamily());
        defaultFont.setPointSizeF(rootStyle.fontSize());
        if (rootStyle.hasFontWeight())
            defaultFont.setWeight(rootStyle.fontWeight());
        if (rootStyle.hasFontItalic())
            defaultFont.setItalic(rootStyle.fontItalic());
        m_document->setDefaultFont(defaultFont);
    } else {
        QFont defaultFont(QStringLiteral("Noto Serif"), 11);
        m_document->setDefaultFont(defaultFont);
    }

    // Safety net: force dark text so system dark-theme palette doesn't bleed through
    m_document->setDefaultStyleSheet(QStringLiteral("body { color: #1a1a1a; }"));

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB
                 | MD_FLAG_UNDERLINE
                 | MD_FLAG_WIKILINKS
                 | MD_FLAG_LATEXMATHSPANS;
    parser.enter_block = &DocumentBuilder::sEnterBlock;
    parser.leave_block = &DocumentBuilder::sLeaveBlock;
    parser.enter_span  = &DocumentBuilder::sEnterSpan;
    parser.leave_span  = &DocumentBuilder::sLeaveSpan;
    parser.text        = &DocumentBuilder::sText;

    const QByteArray utf8 = processedMarkdown.toUtf8();
    int result = md_parse(utf8.constData(), static_cast<MD_SIZE>(utf8.size()),
                          &parser, this);

    // Append footnotes section at end of document
    if (!m_footnotes.isEmpty())
        appendFootnotes();

    return result == 0;
}

void DocumentBuilder::setBasePath(const QString &basePath)
{
    m_basePath = basePath;
}

void DocumentBuilder::setStyleManager(StyleManager *sm)
{
    m_styleManager = sm;
}

void DocumentBuilder::setHyphenator(Hyphenator *hyph)
{
    m_hyphenator = hyph;
}

void DocumentBuilder::setShortWords(ShortWords *sw)
{
    m_shortWords = sw;
}

void DocumentBuilder::setFootnoteStyle(const FootnoteStyle &style)
{
    m_footnoteStyle = style;
}

QString DocumentBuilder::processTypography(const QString &text) const
{
    if (text.isEmpty())
        return text;

    QString result = text;

    // Apply short-words (non-breaking spaces after short prepositions/articles)
    if (m_shortWords)
        result = m_shortWords->process(result);

    // Apply hyphenation (insert soft hyphens at valid break points)
    if (m_hyphenator && m_hyphenator->isLoaded())
        result = m_hyphenator->hyphenateText(result);

    return result;
}

void DocumentBuilder::applyParagraphStyle(const QString &styleName)
{
    if (!m_styleManager)
        return;
    // Resolve through parent chain so inherited properties are included
    ParagraphStyle resolved = m_styleManager->resolvedParagraphStyle(styleName);
    QTextBlockFormat bf = m_cursor.blockFormat();
    resolved.applyBlockFormat(bf);
    m_cursor.setBlockFormat(bf);
    QTextCharFormat cf;
    resolved.applyCharFormat(cf);
    m_cursor.setBlockCharFormat(cf);
    m_cursor.setCharFormat(cf);
}

void DocumentBuilder::applyCharacterStyle(const QString &styleName)
{
    if (!m_styleManager)
        return;
    // Resolve through parent chain so inherited properties are included
    CharacterStyle resolved = m_styleManager->resolvedCharacterStyle(styleName);
    QTextCharFormat cf;
    resolved.applyFormat(cf);
    m_cursor.mergeCharFormat(cf);
}

// --- Static callbacks (delegate to instance) ---

int DocumentBuilder::sEnterBlock(MD_BLOCKTYPE type, void *detail,
                                  void *userdata)
{
    return static_cast<DocumentBuilder *>(userdata)->enterBlock(type, detail);
}

int DocumentBuilder::sLeaveBlock(MD_BLOCKTYPE type, void *detail,
                                  void *userdata)
{
    return static_cast<DocumentBuilder *>(userdata)->leaveBlock(type, detail);
}

int DocumentBuilder::sEnterSpan(MD_SPANTYPE type, void *detail,
                                 void *userdata)
{
    return static_cast<DocumentBuilder *>(userdata)->enterSpan(type, detail);
}

int DocumentBuilder::sLeaveSpan(MD_SPANTYPE type, void *detail,
                                 void *userdata)
{
    return static_cast<DocumentBuilder *>(userdata)->leaveSpan(type, detail);
}

int DocumentBuilder::sText(MD_TEXTTYPE type, const MD_CHAR *text,
                            MD_SIZE size, void *userdata)
{
    return static_cast<DocumentBuilder *>(userdata)->onText(type, text, size);
}

// --- Instance block handlers ---

int DocumentBuilder::enterBlock(MD_BLOCKTYPE type, void *detail)
{
    switch (type) {
    case MD_BLOCK_DOC:
        break;

    case MD_BLOCK_P: {
        // If LI already created a block for us, reuse it; otherwise make a new one
        if (m_listItemBlockReady) {
            m_listItemBlockReady = false;
        } else {
            ensureBlock();
        }

        if (m_blockQuoteLevel > 0) {
            QTextBlockFormat bf = blockQuoteBlockFormat(m_blockQuoteLevel);
            m_cursor.setBlockFormat(bf);
            if (m_styleManager) {
                applyParagraphStyle(QStringLiteral("BlockQuote"));
                // Re-apply blockquote-specific margins
                QTextBlockFormat bfm = m_cursor.blockFormat();
                bfm.setProperty(QTextFormat::BlockQuoteLevel, m_blockQuoteLevel);
                bfm.setLeftMargin(20.0 * m_blockQuoteLevel);
                m_cursor.setBlockFormat(bfm);
            } else {
                m_cursor.setBlockCharFormat(blockQuoteCharFormat());
                m_cursor.setCharFormat(blockQuoteCharFormat());
            }
        } else if (!m_listStack.isEmpty()) {
            // Inside a list — ListItem style was already applied by LI handler.
            // Don't overwrite with BodyText.
        } else {
            m_cursor.setBlockFormat(bodyBlockFormat());
            if (m_styleManager) {
                applyParagraphStyle(QStringLiteral("BodyText"));
            } else {
                QTextCharFormat cf;
                m_cursor.setBlockCharFormat(cf);
                m_cursor.setCharFormat(cf);
            }
        }
        break;
    }

    case MD_BLOCK_H: {
        auto *d = static_cast<MD_BLOCK_H_DETAIL *>(detail);
        int level = static_cast<int>(d->level);
        if (m_listItemBlockReady) {
            m_listItemBlockReady = false;
        } else {
            ensureBlock();
        }
        if (m_styleManager) {
            QString styleName = QStringLiteral("Heading%1").arg(level);
            QTextBlockFormat bf;
            bf.setHeadingLevel(level);
            m_cursor.setBlockFormat(bf);
            applyParagraphStyle(styleName);
        } else {
            m_cursor.setBlockFormat(headingBlockFormat(level));
            m_cursor.setBlockCharFormat(headingCharFormat(level));
            m_cursor.setCharFormat(headingCharFormat(level));
        }
        break;
    }

    case MD_BLOCK_QUOTE:
        m_blockQuoteLevel++;
        break;

    case MD_BLOCK_UL: {
        QTextListFormat listFmt;
        int depth = m_listStack.size() + 1;
        listFmt.setIndent(depth);
        switch (depth) {
        case 1: listFmt.setStyle(QTextListFormat::ListDisc); break;
        case 2: listFmt.setStyle(QTextListFormat::ListCircle); break;
        default: listFmt.setStyle(QTextListFormat::ListSquare); break;
        }
        // Defer list creation to first content block inside MD_BLOCK_LI
        m_listStack.push({listFmt, nullptr});
        break;
    }

    case MD_BLOCK_OL: {
        auto *d = static_cast<MD_BLOCK_OL_DETAIL *>(detail);
        QTextListFormat listFmt;
        listFmt.setStyle(QTextListFormat::ListDecimal);
        listFmt.setIndent(m_listStack.size() + 1);
        listFmt.setStart(static_cast<int>(d->start));
        m_listStack.push({listFmt, nullptr});
        break;
    }

    case MD_BLOCK_LI: {
        auto *d = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
        ensureBlock();

        // Create or add to the list
        if (!m_listStack.isEmpty()) {
            auto &info = m_listStack.top();
            if (!info.list) {
                // First item — createList makes the current block the first item
                info.list = m_cursor.createList(info.format);
            } else {
                info.list->add(m_cursor.block());
            }
        }

        // Task list markers
        if (d->is_task) {
            QTextBlockFormat bf = m_cursor.blockFormat();
            bf.setMarker(d->task_mark == ' '
                         ? QTextBlockFormat::MarkerType::Unchecked
                         : QTextBlockFormat::MarkerType::Checked);
            m_cursor.setBlockFormat(bf);
        }

        // Apply ListItem paragraph style
        if (m_styleManager) {
            applyParagraphStyle(QStringLiteral("ListItem"));
            QTextBlockFormat bf = m_cursor.blockFormat();
            bf.setIndent(m_listStack.size());
            m_cursor.setBlockFormat(bf);
        }

        // Tell the next child block (P, H) to reuse this block
        m_listItemBlockReady = true;
        break;
    }

    case MD_BLOCK_CODE: {
        auto *d = static_cast<MD_BLOCK_CODE_DETAIL *>(detail);
        m_codeLanguage = extractAttribute(d->lang);
        m_inCodeBlock = true;
        ensureBlock();

        // Wrap code block in a QTextFrame for unified background
        QTextFrameFormat frameFmt;
        QColor bgColor(0xf6, 0xf8, 0xfa);
        if (m_styleManager) {
            ParagraphStyle codeStyle = m_styleManager->resolvedParagraphStyle(QStringLiteral("CodeBlock"));
            if (codeStyle.hasBackground())
                bgColor = codeStyle.background();
        }
        frameFmt.setBackground(bgColor);
        frameFmt.setPadding(8.0);
        frameFmt.setMargin(0.0);
        frameFmt.setLeftMargin(12.0);
        frameFmt.setRightMargin(12.0);
        frameFmt.setTopMargin(6.0);
        frameFmt.setBottomMargin(6.0);
        frameFmt.setBorder(0.5);
        frameFmt.setBorderBrush(QColor(0xe1, 0xe4, 0xe8));
        frameFmt.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        m_codeFrame = m_cursor.insertFrame(frameFmt);

        // Apply char format inside the frame (no per-block background)
        QTextBlockFormat bf;
        bf.setTopMargin(1.0);
        bf.setBottomMargin(1.0);
        if (!m_codeLanguage.isEmpty())
            bf.setProperty(QTextFormat::BlockCodeLanguage, m_codeLanguage);
        if (d->fence_char)
            bf.setProperty(QTextFormat::BlockCodeFence, QChar(d->fence_char));
        m_cursor.setBlockFormat(bf);
        if (m_styleManager) {
            // Apply only char-level formatting from CodeBlock style (bg handled by frame)
            ParagraphStyle style = m_styleManager->resolvedParagraphStyle(QStringLiteral("CodeBlock"));
            QTextCharFormat cf;
            style.applyCharFormat(cf);
            m_cursor.setBlockCharFormat(cf);
            m_cursor.setCharFormat(cf);
        } else {
            m_cursor.setBlockCharFormat(codeBlockCharFormat());
            m_cursor.setCharFormat(codeBlockCharFormat());
        }
        break;
    }

    case MD_BLOCK_HR: {
        ensureBlock();
        QTextBlockFormat bf;
        bf.setProperty(QTextFormat::BlockTrailingHorizontalRulerWidth,
                       QTextLength(QTextLength::PercentageLength, 100));
        bf.setTopMargin(12.0);
        bf.setBottomMargin(12.0);
        m_cursor.setBlockFormat(bf);
        break;
    }

    case MD_BLOCK_HTML: {
        ensureBlock();
        QTextBlockFormat bf;
        QTextCharFormat cf;
        QFont mono(QStringLiteral("JetBrains Mono"), 10);
        mono.setStyleHint(QFont::Monospace);
        cf.setFont(mono);
        cf.setForeground(QColor(0x88, 0x88, 0x88));
        m_cursor.setBlockFormat(bf);
        m_cursor.setBlockCharFormat(cf);
        m_cursor.setCharFormat(cf);
        break;
    }

    case MD_BLOCK_TABLE: {
        auto *d = static_cast<MD_BLOCK_TABLE_DETAIL *>(detail);
        ensureBlock();

        // Apply table style from theme if available
        TableStyle ts;
        if (m_styleManager) {
            TableStyle *themed = m_styleManager->tableStyle(QStringLiteral("Default"));
            if (themed)
                ts = *themed;
        }

        QTextTableFormat tableFmt;
        if (ts.hasOuterBorder()) {
            tableFmt.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            tableFmt.setBorder(ts.outerBorder().width);
            tableFmt.setBorderBrush(ts.outerBorder().color);
        } else {
            tableFmt.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            tableFmt.setBorder(0.5);
            tableFmt.setBorderBrush(QColor(0xdd, 0xdd, 0xdd));
        }
        QMarginsF padding = ts.cellPadding();
        tableFmt.setCellPadding(padding.top()); // Qt uses a single padding value
        tableFmt.setCellSpacing(0.0);
        tableFmt.setAlignment(Qt::AlignLeft);

        m_currentTable = m_cursor.insertTable(1, static_cast<int>(d->col_count), tableFmt);
        m_tableRow = -1;
        m_tableCol = 0;
        break;
    }

    case MD_BLOCK_THEAD:
        m_inTableHeader = true;
        break;

    case MD_BLOCK_TBODY:
        m_inTableHeader = false;
        break;

    case MD_BLOCK_TR: {
        m_tableRow++;
        m_tableCol = 0;
        if (m_currentTable && m_tableRow > 0) {
            m_currentTable->appendRows(1);
        }
        break;
    }

    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        if (m_currentTable) {
            QTextTableCell cell = m_currentTable->cellAt(m_tableRow, m_tableCol);
            m_cursor = cell.firstCursorPosition();

            auto *d = static_cast<MD_BLOCK_TD_DETAIL *>(detail);
            QTextBlockFormat bf;
            switch (d->align) {
            case MD_ALIGN_CENTER: bf.setAlignment(Qt::AlignCenter); break;
            case MD_ALIGN_RIGHT:  bf.setAlignment(Qt::AlignRight);  break;
            default:              bf.setAlignment(Qt::AlignLeft);    break;
            }
            m_cursor.setBlockFormat(bf);

            // Resolve table style
            TableStyle ts;
            if (m_styleManager) {
                TableStyle *themed = m_styleManager->tableStyle(QStringLiteral("Default"));
                if (themed)
                    ts = *themed;
            }

            QTextCharFormat cf;
            if (type == MD_BLOCK_TH) {
                cf.setFontWeight(QFont::Bold);
                if (m_styleManager)
                    applyParagraphStyle(ts.headerParagraphStyle());
            } else {
                if (m_styleManager)
                    applyParagraphStyle(ts.bodyParagraphStyle());
            }
            m_cursor.setBlockCharFormat(cf);
            m_cursor.setCharFormat(cf);

            QTextTableCellFormat cellFmt;
            if (m_inTableHeader) {
                if (ts.hasHeaderBackground())
                    cellFmt.setBackground(ts.headerBackground());
                else
                    cellFmt.setBackground(QColor(0xf0, 0xf0, 0xf0));
                if (ts.hasHeaderForeground())
                    cellFmt.setForeground(ts.headerForeground());
            } else {
                // Alternating row colors
                if (ts.hasAlternateRowColor() && (m_tableRow % (ts.alternateFrequency() * 2)) >= ts.alternateFrequency()) {
                    cellFmt.setBackground(ts.alternateRowColor());
                } else if (ts.hasBodyBackground()) {
                    cellFmt.setBackground(ts.bodyBackground());
                }
            }
            cell.setFormat(cellFmt);
        }
        break;
    }
    }

    return 0;
}

int DocumentBuilder::leaveBlock(MD_BLOCKTYPE type, void *detail)
{
    Q_UNUSED(detail);

    switch (type) {
    case MD_BLOCK_QUOTE:
        m_blockQuoteLevel--;
        break;

    case MD_BLOCK_LI:
        m_listItemBlockReady = false;
        break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (!m_listStack.isEmpty())
            m_listStack.pop();
        break;

    case MD_BLOCK_CODE:
        m_inCodeBlock = false;
        m_codeLanguage.clear();
        // Move cursor out of the code frame
        if (m_codeFrame) {
            m_cursor = QTextCursor(m_document);
            m_cursor.movePosition(QTextCursor::End);
            m_codeFrame = nullptr;
        }
        break;

    case MD_BLOCK_TABLE:
        if (m_currentTable) {
            m_cursor = QTextCursor(m_document);
            m_cursor.movePosition(QTextCursor::End);
            m_currentTable = nullptr;
        }
        break;

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        m_tableCol++;
        break;

    default:
        break;
    }

    return 0;
}

// --- Span handlers ---

int DocumentBuilder::enterSpan(MD_SPANTYPE type, void *detail)
{
    switch (type) {
    case MD_SPAN_EM: {
        m_charFormatStack.push(m_cursor.charFormat());
        QTextCharFormat fmt;
        fmt.setFontItalic(true);
        m_cursor.mergeCharFormat(fmt);
        break;
    }

    case MD_SPAN_STRONG: {
        m_charFormatStack.push(m_cursor.charFormat());
        QTextCharFormat fmt;
        fmt.setFontWeight(QFont::Bold);
        m_cursor.mergeCharFormat(fmt);
        break;
    }

    case MD_SPAN_CODE: {
        m_charFormatStack.push(m_cursor.charFormat());
        if (m_styleManager) {
            applyCharacterStyle(QStringLiteral("InlineCode"));
        } else {
            QTextCharFormat fmt;
            QFont mono(QStringLiteral("JetBrains Mono"), 10);
            mono.setStyleHint(QFont::Monospace);
            fmt.setFont(mono);
            fmt.setForeground(QColor(0xc7, 0x25, 0x4e));
            fmt.setBackground(QColor(0xf0, 0xf0, 0xf0));
            m_cursor.mergeCharFormat(fmt);
        }
        break;
    }

    case MD_SPAN_A: {
        auto *d = static_cast<MD_SPAN_A_DETAIL *>(detail);
        m_charFormatStack.push(m_cursor.charFormat());
        QTextCharFormat fmt;
        fmt.setAnchor(true);
        fmt.setAnchorHref(extractAttribute(d->href));
        QString title = extractAttribute(d->title);
        if (!title.isEmpty())
            fmt.setToolTip(title);
        m_cursor.mergeCharFormat(fmt);
        if (m_styleManager) {
            applyCharacterStyle(QStringLiteral("Link"));
        } else {
            QTextCharFormat linkFmt;
            linkFmt.setForeground(QColor(0x03, 0x66, 0xd6));
            linkFmt.setFontUnderline(true);
            m_cursor.mergeCharFormat(linkFmt);
        }
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

    case MD_SPAN_DEL: {
        m_charFormatStack.push(m_cursor.charFormat());
        QTextCharFormat fmt;
        fmt.setFontStrikeOut(true);
        m_cursor.mergeCharFormat(fmt);
        break;
    }

    case MD_SPAN_U: {
        m_charFormatStack.push(m_cursor.charFormat());
        QTextCharFormat fmt;
        fmt.setFontUnderline(true);
        m_cursor.mergeCharFormat(fmt);
        break;
    }

    case MD_SPAN_WIKILINK: {
        auto *d = static_cast<MD_SPAN_WIKILINK_DETAIL *>(detail);
        m_charFormatStack.push(m_cursor.charFormat());
        QTextCharFormat fmt;
        fmt.setAnchor(true);
        fmt.setAnchorHref(QStringLiteral("wiki:") + extractAttribute(d->target));
        fmt.setForeground(QColor(0x03, 0x66, 0xd6));
        fmt.setFontUnderline(true);
        m_cursor.mergeCharFormat(fmt);
        break;
    }

    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY: {
        // Placeholder: render as inline code
        m_charFormatStack.push(m_cursor.charFormat());
        QTextCharFormat fmt;
        QFont mono(QStringLiteral("JetBrains Mono"), 10);
        mono.setStyleHint(QFont::Monospace);
        fmt.setFont(mono);
        fmt.setForeground(QColor(0x6a, 0x3d, 0x9a));
        fmt.setBackground(QColor(0xf5, 0xf0, 0xff));
        m_cursor.mergeCharFormat(fmt);
        break;
    }
    }

    return 0;
}

int DocumentBuilder::leaveSpan(MD_SPANTYPE type, void *detail)
{
    Q_UNUSED(detail);

    switch (type) {
    case MD_SPAN_IMG: {
        m_collectingAltText = false;

        // Resolve image path
        QString src = m_imageSrc;
        QString resolved;
        if (QFileInfo(src).isRelative() && !m_basePath.isEmpty()) {
            resolved = QDir(m_basePath).filePath(src);
        } else {
            resolved = src;
        }

        if (!PenelopeSettings::self()->renderImages()) {
            m_cursor.insertText(m_altText);
            m_imageSrc.clear(); m_imageTitle.clear(); m_altText.clear();
            break;
        }
        QImage image(resolved);
        if (!image.isNull()) {
            QUrl url = QUrl(QStringLiteral("pretty://img/") + src);
            m_document->addResource(QTextDocument::ImageResource, url, image);

            QTextImageFormat imgFmt;
            imgFmt.setName(url.toString());
            if (!m_altText.isEmpty())
                imgFmt.setProperty(QTextFormat::ImageAltText, m_altText);
            if (!m_imageTitle.isEmpty())
                imgFmt.setToolTip(m_imageTitle);

            // Scale to fit available width
            qreal maxWidth = 600.0;
            QSizeF pageSize = m_document->pageSize();
            if (pageSize.width() > 0)
                maxWidth = pageSize.width() * 0.9; // 90% of page width

            if (image.width() > maxWidth) {
                qreal ratio = maxWidth / image.width();
                imgFmt.setWidth(maxWidth);
                imgFmt.setHeight(image.height() * ratio);
            }

            m_cursor.insertImage(imgFmt);
        } else {
            // Image not found -- insert placeholder text
            QTextCharFormat fmt;
            fmt.setForeground(QColor(0xaa, 0x33, 0x33));
            fmt.setFontItalic(true);
            m_cursor.insertText(
                QStringLiteral("[Image: %1]").arg(
                    m_altText.isEmpty() ? src : m_altText),
                fmt);
        }

        m_imageSrc.clear();
        m_imageTitle.clear();
        m_altText.clear();
        break;
    }

    default:
        if (!m_charFormatStack.isEmpty())
            m_cursor.setCharFormat(m_charFormatStack.pop());
        break;
    }

    return 0;
}

// --- Text handler ---

int DocumentBuilder::onText(MD_TEXTTYPE type, const MD_CHAR *text,
                             MD_SIZE size)
{
    QString str = QString::fromUtf8(text, static_cast<int>(size));

    if (m_collectingAltText) {
        m_altText.append(str);
        return 0;
    }

    switch (type) {
    case MD_TEXT_NORMAL: {
        // Check for footnote references [^label]
        if (!m_footnotes.isEmpty()) {
            static const QRegularExpression fnRefRx(
                QStringLiteral(R"(\[\^([^\]]+)\])"));
            int lastEnd = 0;
            auto it = fnRefRx.globalMatch(str);
            bool found = false;
            while (it.hasNext()) {
                auto match = it.next();
                // Insert text before the reference (with typography)
                if (match.capturedStart() > lastEnd) {
                    QString segment = str.mid(lastEnd, match.capturedStart() - lastEnd);
                    if (!m_inCodeBlock)
                        segment = processTypography(segment);
                    m_cursor.insertText(segment);
                }

                // Find the footnote index
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
                    QString refLabel = m_footnoteStyle.formatNumber(number);
                    QTextCharFormat superFmt;
                    if (m_footnoteStyle.superscriptRef) {
                        superFmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
                        superFmt.setFontPointSize(8);
                    } else {
                        superFmt.setFontPointSize(m_cursor.charFormat().fontPointSize());
                    }
                    superFmt.setForeground(QColor(0x03, 0x66, 0xd6));
                    m_cursor.insertText(refLabel, superFmt);
                    // Restore previous format
                    m_cursor.setCharFormat(m_charFormatStack.isEmpty()
                                           ? QTextCharFormat()
                                           : m_charFormatStack.top());
                } else {
                    m_cursor.insertText(match.captured(0));
                }

                lastEnd = match.capturedEnd();
                found = true;
            }
            if (found) {
                if (lastEnd < str.size()) {
                    QString tail = str.mid(lastEnd);
                    if (!m_inCodeBlock)
                        tail = processTypography(tail);
                    m_cursor.insertText(tail);
                }
            } else {
                // No footnote refs found -- apply typography to full text
                if (!m_inCodeBlock)
                    str = processTypography(str);
                m_cursor.insertText(str);
            }
        } else {
            // No footnotes -- apply typography directly
            if (!m_inCodeBlock)
                str = processTypography(str);
            m_cursor.insertText(str);
        }
        break;
    }

    case MD_TEXT_CODE: {
        if (m_inCodeBlock) {
            // Split lines -- each becomes its own block inside the code frame.
            // Capture the char format from the first block (set by enterBlock
            // from the style manager or fallback) so all lines match.
            const QTextCharFormat codeCf = m_cursor.charFormat();
            const QStringList lines = str.split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i) {
                if (i > 0) {
                    m_cursor.insertBlock();
                    QTextBlockFormat bf;
                    bf.setTopMargin(1.0);
                    bf.setBottomMargin(1.0);
                    if (!m_codeLanguage.isEmpty())
                        bf.setProperty(QTextFormat::BlockCodeLanguage,
                                       m_codeLanguage);
                    m_cursor.setBlockFormat(bf);
                    m_cursor.setBlockCharFormat(codeCf);
                    m_cursor.setCharFormat(codeCf);
                }
                m_cursor.insertText(lines[i]);
            }
        } else {
            // Inline code -- format already set by span handler
            m_cursor.insertText(str);
        }
        break;
    }

    case MD_TEXT_BR:
        m_cursor.insertBlock();
        break;

    case MD_TEXT_SOFTBR:
        m_cursor.insertText(QStringLiteral(" "));
        break;

    case MD_TEXT_ENTITY: {
        QString decoded = resolveEntity(str);
        m_cursor.insertText(decoded);
        break;
    }

    case MD_TEXT_NULLCHAR:
        m_cursor.insertText(QString(QChar(0xFFFD)));
        break;

    case MD_TEXT_HTML:
        // Skip inline HTML in reader mode
        break;

    case MD_TEXT_LATEXMATH:
        m_cursor.insertText(str);
        break;
    }

    return 0;
}

// --- Helpers ---

void DocumentBuilder::ensureBlock()
{
    if (m_isFirstBlock) {
        m_isFirstBlock = false;
    } else {
        m_cursor.insertBlock();
    }
}

QString DocumentBuilder::extractAttribute(const MD_ATTRIBUTE &attr)
{
    if (!attr.text || attr.size == 0)
        return {};

    // Simple case: single normal substring covering the whole attribute
    return QString::fromUtf8(attr.text, static_cast<int>(attr.size));
}

QString DocumentBuilder::resolveEntity(const QString &entity)
{
    // Common HTML entities
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

    // Numeric entities: &#1234; or &#x12AB;
    if (entity.startsWith(QLatin1String("&#"))) {
        QString num = entity.mid(2, entity.size() - 3); // strip &# and ;
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
    }

    // Unknown entity: return as-is
    return entity;
}

// --- Footnote handling ---

void DocumentBuilder::appendFootnotes()
{
    // Separator line before footnotes
    if (m_footnoteStyle.showSeparator) {
        m_cursor.insertBlock();
        QTextBlockFormat hrFmt;
        hrFmt.setProperty(QTextFormat::BlockTrailingHorizontalRulerWidth,
                          QTextLength(QTextLength::FixedLength, m_footnoteStyle.separatorLength));
        hrFmt.setTopMargin(20.0);
        hrFmt.setBottomMargin(8.0);
        m_cursor.setBlockFormat(hrFmt);
    }

    // Render each footnote
    for (int i = 0; i < m_footnotes.size(); ++i) {
        const Footnote &fn = m_footnotes[i];
        int number = m_footnoteStyle.startNumber + i;
        QString label = m_footnoteStyle.formatNumber(number);

        m_cursor.insertBlock();
        QTextBlockFormat bf;
        bf.setBottomMargin(2.0);
        bf.setLeftMargin(20.0);
        bf.setTextIndent(-20.0);
        m_cursor.setBlockFormat(bf);

        // Footnote number
        QTextCharFormat numFmt;
        if (m_footnoteStyle.superscriptNote) {
            numFmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
            numFmt.setFontPointSize(8);
        } else {
            numFmt.setFontPointSize(9);
        }
        numFmt.setForeground(QColor(0x03, 0x66, 0xd6));
        m_cursor.insertText(label, numFmt);

        // Footnote text
        QTextCharFormat textFmt;
        textFmt.setFontPointSize(9);
        textFmt.setForeground(QColor(0x55, 0x55, 0x55));
        m_cursor.insertText(QStringLiteral(" ") + fn.text, textFmt);
    }
}

// --- Default format builders ---

QTextBlockFormat DocumentBuilder::headingBlockFormat(int level)
{
    QTextBlockFormat bf;
    bf.setHeadingLevel(level);

    static const qreal spaceBefore[] = {0, 24, 20, 16, 12, 10, 8};
    static const qreal spaceAfter[]  = {0, 12, 10,  8,  6,  4, 4};
    if (level >= 1 && level <= 6) {
        bf.setTopMargin(spaceBefore[level]);
        bf.setBottomMargin(spaceAfter[level]);
    }
    return bf;
}

QTextCharFormat DocumentBuilder::headingCharFormat(int level)
{
    QTextCharFormat cf;
    cf.setFontWeight(QFont::Bold);

    static const qreal sizes[] = {0, 28, 24, 20, 16, 14, 12};
    if (level >= 1 && level <= 6) {
        cf.setFontPointSize(sizes[level]);
    }

    QFont font(QStringLiteral("Noto Sans"));
    font.setWeight(QFont::Bold);
    font.setPointSizeF(sizes[level]);
    if (level == 6)
        font.setItalic(true);
    cf.setFont(font);

    return cf;
}

QTextBlockFormat DocumentBuilder::bodyBlockFormat()
{
    QTextBlockFormat bf;
    bf.setBottomMargin(6.0);
    return bf;
}

QTextBlockFormat DocumentBuilder::codeBlockBlockFormat()
{
    QTextBlockFormat bf;
    bf.setBackground(QColor(0xf6, 0xf8, 0xfa));
    bf.setLeftMargin(12.0);
    bf.setRightMargin(12.0);
    bf.setTopMargin(2.0);
    bf.setBottomMargin(2.0);
    return bf;
}

QTextCharFormat DocumentBuilder::codeBlockCharFormat()
{
    QTextCharFormat cf;
    QFont mono(QStringLiteral("JetBrains Mono"), 10);
    mono.setStyleHint(QFont::Monospace);
    cf.setFont(mono);
    return cf;
}

QTextBlockFormat DocumentBuilder::blockQuoteBlockFormat(int level)
{
    QTextBlockFormat bf;
    bf.setProperty(QTextFormat::BlockQuoteLevel, level);
    bf.setLeftMargin(20.0 * level);
    bf.setBottomMargin(6.0);
    return bf;
}

QTextCharFormat DocumentBuilder::blockQuoteCharFormat()
{
    QTextCharFormat cf;
    cf.setFontItalic(true);
    cf.setForeground(QColor(0x55, 0x55, 0x55));
    return cf;
}
