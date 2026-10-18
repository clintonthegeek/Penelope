/*
 * contentrtfexporter.cpp — RTF generation from Content:: model nodes
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "contentrtfexporter.h"
#include "codespancollector.h"

#include <QFont>

// --- Public ---

QByteArray ContentRtfExporter::exportBlocks(const QList<Content::BlockNode> &blocks,
                                             const RtfFilterOptions &filter,
                                             const FootnoteStyle &noteStyle)
{
    m_filter = filter;
    m_noteStyle = noteStyle;
    m_fonts.clear();
    m_colors.clear();

    // Ensure default entries
    fontIndex(QStringLiteral("Noto Serif"));
    colorIndex(QColor(Qt::black));
    colorIndex(QColor(Qt::white));

    // Pre-scan all blocks to collect fonts and colors
    scanStyles(blocks);

    QByteArray rtf;
    rtf.reserve(4096);

    rtf.append(writeHeader());

    for (const auto &block : blocks)
        writeBlock(rtf, block);

    rtf.append("}");
    return rtf;
}

// --- Pre-scan ---

void ContentRtfExporter::scanStyles(const QList<Content::BlockNode> &blocks)
{
    for (const auto &block : blocks) {
        std::visit([this](const auto &b) {
            using T = std::decay_t<decltype(b)>;
            if constexpr (std::is_same_v<T, Content::Paragraph>) {
                scanParagraphFormat(b.format);
                scanInlines(b.inlines);
            } else if constexpr (std::is_same_v<T, Content::Heading>) {
                scanParagraphFormat(b.format);
                scanInlines(b.inlines);
            } else if constexpr (std::is_same_v<T, Content::CodeBlock>) {
                scanTextStyle(b.style);
                if (b.background.isValid())
                    colorIndex(b.background);
                // Pre-scan syntax highlighting token colors/fonts
                auto codeInlines = buildCodeInlines(b);
                scanInlines(codeInlines);
            } else if constexpr (std::is_same_v<T, Content::BlockQuote>) {
                scanParagraphFormat(b.format);
                scanStyles(b.children);
            } else if constexpr (std::is_same_v<T, Content::List>) {
                for (const auto &item : b.items)
                    scanStyles(item.children);
            } else if constexpr (std::is_same_v<T, Content::Table>) {
                for (const auto &row : b.rows) {
                    for (const auto &cell : row.cells) {
                        scanTextStyle(cell.style);
                        scanInlines(cell.inlines);
                        if (cell.background.isValid())
                            colorIndex(cell.background);
                    }
                }
                if (b.headerBackground.isValid())
                    colorIndex(b.headerBackground);
                if (b.headerForeground.isValid())
                    colorIndex(b.headerForeground);
                if (b.bodyBackground.isValid())
                    colorIndex(b.bodyBackground);
                if (b.alternateRowColor.isValid())
                    colorIndex(b.alternateRowColor);
                if (b.borderColor.isValid())
                    colorIndex(b.borderColor);
            } else if constexpr (std::is_same_v<T, Content::FootnoteSection>) {
                for (const auto &fn : b.footnotes) {
                    scanTextStyle(fn.numberStyle);
                    scanTextStyle(fn.textStyle);
                    scanStyles(fn.blocks);
                }
            }
            // HorizontalRule has no styles to scan
        }, block);
    }
}

void ContentRtfExporter::scanInlines(const QList<Content::InlineNode> &inlines)
{
    for (const auto &node : inlines) {
        std::visit([this](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Content::TextRun>) {
                scanTextStyle(n.style);
            } else if constexpr (std::is_same_v<T, Content::InlineCode>) {
                scanTextStyle(n.style);
            } else if constexpr (std::is_same_v<T, Content::Link>) {
                scanTextStyle(n.style);
            } else if constexpr (std::is_same_v<T, Content::Math>) {
                scanTextStyle(n.style);
            } else if constexpr (std::is_same_v<T, Content::FootnoteRef>) {
                scanTextStyle(n.style);
            }
        }, node);
    }
}

void ContentRtfExporter::scanTextStyle(const Content::TextStyle &style)
{
    if (!style.fontFamily.isEmpty())
        fontIndex(style.fontFamily);
    if (style.foreground.isValid())
        colorIndex(style.foreground);
    if (style.background.isValid())
        colorIndex(style.background);
}

void ContentRtfExporter::scanParagraphFormat(const Content::ParagraphFormat &fmt)
{
    if (fmt.background.isValid())
        colorIndex(fmt.background);
}

// --- Code block syntax highlighting ---

QList<Content::InlineNode> ContentRtfExporter::buildCodeInlines(const Content::CodeBlock &cb)
{
    QList<Content::InlineNode> inlines;

    if (!cb.language.isEmpty()) {
        CodeSpanCollector collector;
        auto spans = collector.highlight(cb.code, cb.language);

        if (!spans.isEmpty()) {
            std::sort(spans.begin(), spans.end(),
                      [](const auto &a, const auto &b) { return a.start < b.start; });

            int lastEnd = 0;
            for (const auto &span : spans) {
                // Gap between spans → default style
                if (span.start > lastEnd) {
                    Content::TextRun run;
                    run.text = cb.code.mid(lastEnd, span.start - lastEnd);
                    run.style = cb.style;
                    inlines.append(run);
                }
                // Highlighted span
                Content::TextRun run;
                run.text = cb.code.mid(span.start, span.length);
                run.style = cb.style;
                if (span.foreground.isValid())
                    run.style.foreground = span.foreground;
                if (span.background.isValid())
                    run.style.background = span.background;
                if (span.bold)
                    run.style.fontWeight = 700;
                if (span.italic)
                    run.style.italic = true;
                inlines.append(run);
                lastEnd = span.start + span.length;
            }
            // Trailing unstyled text
            if (lastEnd < cb.code.size()) {
                Content::TextRun run;
                run.text = cb.code.mid(lastEnd);
                run.style = cb.style;
                inlines.append(run);
            }
        }
    }

    // Fallback: plain monospace text
    if (inlines.isEmpty()) {
        Content::TextRun run;
        run.text = cb.code;
        run.style = cb.style;
        inlines.append(run);
    }

    return inlines;
}

// --- RTF generation ---

QByteArray ContentRtfExporter::writeHeader()
{
    QByteArray hdr;
    hdr.append("{\\rtf1\\ansi\\deff0\n");

    // Font table
    hdr.append("{\\fonttbl");
    for (auto it = m_fonts.constBegin(); it != m_fonts.constEnd(); ++it) {
        hdr.append("{\\f");
        hdr.append(QByteArray::number(it.value()));

        // Determine font family type hint
        QFont testFont(it.key());
        switch (testFont.styleHint()) {
        case QFont::Serif:     hdr.append("\\froman "); break;
        case QFont::SansSerif: hdr.append("\\fswiss "); break;
        case QFont::Monospace: hdr.append("\\fmodern "); break;
        default:               hdr.append("\\fnil ");   break;
        }

        hdr.append(it.key().toLatin1());
        hdr.append(";}");
    }
    hdr.append("}\n");

    // Color table — RTF color indices are 1-based; index 0 is auto/default
    // We write a leading ';' for the auto color entry, then each registered color.
    hdr.append("{\\colortbl;");

    // Build ordered list from map
    QList<QPair<QRgb, int>> colorList;
    for (auto it = m_colors.constBegin(); it != m_colors.constEnd(); ++it)
        colorList.append({it.key(), it.value()});
    std::sort(colorList.begin(), colorList.end(),
              [](const auto &a, const auto &b) { return a.second < b.second; });

    for (const auto &entry : colorList) {
        QColor c = QColor::fromRgb(entry.first);
        hdr.append("\\red");
        hdr.append(QByteArray::number(c.red()));
        hdr.append("\\green");
        hdr.append(QByteArray::number(c.green()));
        hdr.append("\\blue");
        hdr.append(QByteArray::number(c.blue()));
        hdr.append(";");
    }
    hdr.append("}\n");

    hdr.append("\\viewkind4\\uc1\\pard\n");
    return hdr;
}

void ContentRtfExporter::writeBlock(QByteArray &out, const Content::BlockNode &block)
{
    std::visit([this, &out](const auto &b) {
        using T = std::decay_t<decltype(b)>;
        if constexpr (std::is_same_v<T, Content::Paragraph>) {
            writeParagraph(out, b);
        } else if constexpr (std::is_same_v<T, Content::Heading>) {
            writeHeading(out, b);
        } else if constexpr (std::is_same_v<T, Content::CodeBlock>) {
            writeCodeBlock(out, b);
        } else if constexpr (std::is_same_v<T, Content::BlockQuote>) {
            writeBlockQuote(out, b);
        } else if constexpr (std::is_same_v<T, Content::List>) {
            writeList(out, b);
        } else if constexpr (std::is_same_v<T, Content::Table>) {
            writeTable(out, b);
        } else if constexpr (std::is_same_v<T, Content::HorizontalRule>) {
            writeHorizontalRule(out);
        } else if constexpr (std::is_same_v<T, Content::FootnoteSection>) {
            // Footnotes: write a separator line then each footnote
            writeHorizontalRule(out);
            for (const auto &fn : b.footnotes) {
                out.append("\\pard\\ql\\sb60\\sa40 ");
                out.append("{");
                writeCharFormat(out, fn.numberStyle);
                out.append(RtfUtils::escapeText(Content::footnoteMarker(m_noteStyle, fn.index)));
                out.append(".} ");
                for (const auto &noteBlock : fn.blocks)
                    writeBlock(out, noteBlock);
                out.append("\\par\n");
            }
        }
    }, block);
}

void ContentRtfExporter::writeParagraph(QByteArray &out, const Content::Paragraph &para)
{
    out.append("\\pard");
    writeParagraphFormat(out, para.format);
    out.append(" ");
    writeInlines(out, para.inlines);
    out.append("\\par\n");
}

void ContentRtfExporter::writeHeading(QByteArray &out, const Content::Heading &heading)
{
    out.append("\\pard");
    out.append("\\s");
    out.append(QByteArray::number(heading.level));
    writeParagraphFormat(out, heading.format);
    out.append(" ");
    writeInlines(out, heading.inlines);
    out.append("\\par\n");
}

void ContentRtfExporter::writeCodeBlock(QByteArray &out, const Content::CodeBlock &cb)
{
    int bgIdx = (m_filter.includeHighlights && cb.background.isValid())
                    ? colorIndex(cb.background) + 1 : 0;

    // Build syntax-highlighted inline list (same approach as layout engine)
    QList<Content::InlineNode> allInlines = buildCodeInlines(cb);

    // Split the inline TextRuns at newline boundaries into per-line groups.
    // Each group becomes one RTF paragraph with \cbpat background.
    QList<QList<Content::TextRun>> lines;
    lines.emplaceBack();

    for (const auto &node : allInlines) {
        auto *tr = std::get_if<Content::TextRun>(&node);
        if (!tr)
            continue;

        // Split this TextRun at '\n' characters
        const QStringList parts = tr->text.split(QLatin1Char('\n'));
        for (int i = 0; i < parts.size(); ++i) {
            if (i > 0)
                lines.emplaceBack(); // newline → start new line group
            if (!parts[i].isEmpty()) {
                Content::TextRun partRun;
                partRun.text = parts[i];
                partRun.style = tr->style;
                lines.last().append(partRun);
            }
        }
    }

    // Write each line as a paragraph
    for (const auto &lineRuns : lines) {
        out.append("\\pard\\ql");
        if (bgIdx > 0) {
            out.append("\\cbpat");
            out.append(QByteArray::number(bgIdx));
        }
        out.append(" ");

        if (lineRuns.isEmpty()) {
            // Empty line — still emit a group so the paragraph has content
            out.append("{");
            writeCharFormat(out, cb.style);
            out.append("}");
        } else {
            for (const auto &run : lineRuns) {
                out.append("{");
                writeCharFormat(out, run.style);
                out.append(RtfUtils::escapeText(run.text));
                out.append("}");
            }
        }

        out.append("\\par\n");
    }
}

void ContentRtfExporter::writeList(QByteArray &out, const Content::List &list, int depth)
{
    int itemNumber = list.startNumber;
    for (const auto &item : list.items) {
        // Generate bullet/number text
        QByteArray pnText;
        if (list.type == Content::ListType::Ordered) {
            pnText = QByteArray::number(itemNumber) + ".\\tab";
            ++itemNumber;
        } else {
            if (item.isTask) {
                pnText = item.taskChecked ? "[x]\\tab" : "[ ]\\tab";
            } else {
                pnText = "\\'B7\\tab";
            }
        }

        int indentTwips = 720 * (depth + 1);

        // Write child blocks of this list item
        for (int i = 0; i < item.children.size(); ++i) {
            const auto &child = item.children[i];

            // Only add bullet/number to the first child block
            if (i == 0) {
                // If first child is a paragraph, merge bullet into it
                if (auto *para = std::get_if<Content::Paragraph>(&child)) {
                    out.append("\\pard\\li");
                    out.append(QByteArray::number(indentTwips));
                    out.append("\\fi-360");
                    writeParagraphFormat(out, para->format);
                    out.append("{\\pntext ");
                    out.append(pnText);
                    out.append("}");
                    writeInlines(out, para->inlines);
                    out.append("\\par\n");
                    continue;
                }
            }

            // Nested list or other block type: write recursively
            if (auto *nestedList = std::get_if<Content::List>(&child)) {
                writeList(out, *nestedList, depth + 1);
            } else {
                writeBlock(out, child);
            }
        }
    }
}

void ContentRtfExporter::writeTable(QByteArray &out, const Content::Table &table)
{
    if (table.rows.isEmpty())
        return;

    int cols = 0;
    for (const auto &row : table.rows)
        cols = qMax(cols, row.cells.size());
    if (cols == 0)
        return;

    int pageWidthTwips = 9360; // ~6.5 inches

    for (int rowIdx = 0; rowIdx < table.rows.size(); ++rowIdx) {
        const auto &row = table.rows[rowIdx];
        bool isHeaderRow = rowIdx < table.headerRowCount;

        // Row definition
        out.append("\\trowd\\trqc");
        if (isHeaderRow)
            out.append("\\trhdr"); // repeat header on page breaks

        // Cell borders
        for (int c = 0; c < cols; ++c) {
            // Cell border definitions
            out.append("\\clbrdrt\\brdrs\\brdrw10");
            out.append("\\clbrdrb\\brdrs\\brdrw10");
            out.append("\\clbrdrl\\brdrs\\brdrw10");
            out.append("\\clbrdrr\\brdrs\\brdrw10");

            // Cell background
            QColor bg;
            if (isHeaderRow && table.headerBackground.isValid())
                bg = table.headerBackground;
            else if (c < row.cells.size() && row.cells[c].background.isValid())
                bg = row.cells[c].background;
            else if (!isHeaderRow && table.alternateRowColor.isValid() && (rowIdx % 2 == 1))
                bg = table.alternateRowColor;
            else if (!isHeaderRow && table.bodyBackground.isValid())
                bg = table.bodyBackground;

            if (m_filter.includeHighlights && bg.isValid()) {
                out.append("\\clcbpat");
                out.append(QByteArray::number(colorIndex(bg) + 1));
            }

            int rightEdge = pageWidthTwips * (c + 1) / cols;
            out.append("\\cellx");
            out.append(QByteArray::number(rightEdge));
        }
        out.append("\n");

        // Cell contents
        for (int c = 0; c < cols; ++c) {
            out.append("\\pard\\intbl");

            if (c < row.cells.size()) {
                const auto &cell = row.cells[c];

                // Alignment
                if (m_filter.includeAlignment) {
                    if (cell.alignment & Qt::AlignHCenter)
                        out.append("\\qc");
                    else if (cell.alignment & Qt::AlignRight)
                        out.append("\\qr");
                    else
                        out.append("\\ql");
                }

                out.append(" ");

                // Determine foreground override for header rows
                QColor fgOverride;
                if (m_filter.includeTextColor && isHeaderRow && table.headerForeground.isValid())
                    fgOverride = table.headerForeground;

                writeInlines(out, cell.inlines, fgOverride);
            } else {
                out.append("\\ql ");
            }
            out.append("\\cell\n");
        }

        out.append("\\row\n");
    }
}

void ContentRtfExporter::writeHorizontalRule(QByteArray &out)
{
    out.append("\\pard\\brdrb\\brdrs\\brdrw10\\brsp20\\par\n");
}

void ContentRtfExporter::writeBlockQuote(QByteArray &out, const Content::BlockQuote &bq)
{
    // Render block quote children with left indentation
    int indentTwips = 720 * bq.level;
    for (const auto &child : bq.children) {
        if (auto *para = std::get_if<Content::Paragraph>(&child)) {
            out.append("\\pard\\li");
            out.append(QByteArray::number(indentTwips));
            writeParagraphFormat(out, para->format);
            out.append(" ");
            writeInlines(out, para->inlines);
            out.append("\\par\n");
        } else if (auto *nestedBq = std::get_if<Content::BlockQuote>(&child)) {
            writeBlockQuote(out, *nestedBq);
        } else {
            writeBlock(out, child);
        }
    }
}

void ContentRtfExporter::writeInlines(QByteArray &out, const QList<Content::InlineNode> &inlines,
                                       const QColor &foregroundOverride)
{
    // When source formatting is off, use the first TextRun's style uniformly
    // for all inline nodes, stripping per-word bold/italic/code/link differences.
    Content::TextStyle baseStyle;
    bool useBaseStyle = !m_filter.includeSourceFormatting;
    if (useBaseStyle) {
        for (const auto &node : inlines) {
            if (auto *tr = std::get_if<Content::TextRun>(&node)) {
                baseStyle = tr->style;
                break;
            }
        }
    }

    for (const auto &node : inlines) {
        std::visit([this, &out, &baseStyle, useBaseStyle, &foregroundOverride](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Content::TextRun>) {
                Content::TextStyle style = useBaseStyle ? baseStyle : n.style;
                // Apply foreground override if the run uses default color
                if (foregroundOverride.isValid() && !n.style.foreground.isValid())
                    style.foreground = foregroundOverride;
                out.append("{");
                writeCharFormat(out, style);
                out.append(RtfUtils::escapeText(n.text));
                out.append("}");
            } else if constexpr (std::is_same_v<T, Content::InlineCode>) {
                out.append("{");
                writeCharFormat(out, useBaseStyle ? baseStyle : n.style);
                out.append(RtfUtils::escapeText(n.text));
                out.append("}");
            } else if constexpr (std::is_same_v<T, Content::Link>) {
                out.append("{");
                writeCharFormat(out, useBaseStyle ? baseStyle : n.style);
                out.append(RtfUtils::escapeText(n.text));
                out.append("}");
            } else if constexpr (std::is_same_v<T, Content::Math>) {
                Content::TextStyle mathStyle = n.style;
                mathStyle.italic = true;
                out.append("{");
                writeCharFormat(out, mathStyle);
                out.append(RtfUtils::escapeText(n.source));
                out.append("}");
            } else if constexpr (std::is_same_v<T, Content::FootnoteRef>) {
                out.append("{");
                writeCharFormat(out, useBaseStyle ? baseStyle : n.style);
                out.append(RtfUtils::escapeText(Content::footnoteMarker(m_noteStyle, n.index)));
                out.append("}");
            } else if constexpr (std::is_same_v<T, Content::InlineImage>) {
                // Images cannot be inlined in clipboard RTF; emit alt text
                if (!n.altText.isEmpty())
                    out.append(RtfUtils::escapeText(QStringLiteral("[%1]").arg(n.altText)));
            } else if constexpr (std::is_same_v<T, Content::SoftBreak>) {
                out.append(" ");
            } else if constexpr (std::is_same_v<T, Content::HardBreak>) {
                out.append("\\line ");
            }
        }, node);
    }
}

void ContentRtfExporter::writeCharFormat(QByteArray &out, const Content::TextStyle &style)
{
    // Font
    if (m_filter.includeFonts) {
        if (!style.fontFamily.isEmpty()) {
            out.append("\\f");
            out.append(QByteArray::number(fontIndex(style.fontFamily)));
        }

        // Font size in half-points
        if (style.fontSize > 0) {
            out.append("\\fs");
            out.append(QByteArray::number(RtfUtils::toHalfPoints(style.fontSize)));
        }
    }

    if (m_filter.includeEmphasis) {
        // Bold
        if (style.fontWeight >= 700)
            out.append("\\b");

        // Italic
        if (style.italic)
            out.append("\\i");

        // Underline
        if (style.underline)
            out.append("\\ul");

        // Strikethrough
        if (style.strikethrough)
            out.append("\\strike");
    }

    // Superscript / subscript
    if (m_filter.includeScripts) {
        if (style.superscript)
            out.append("\\super");
        else if (style.subscript)
            out.append("\\sub");
    }

    // Foreground color (RTF color table is 1-based)
    if (m_filter.includeTextColor) {
        if (style.foreground.isValid()) {
            out.append("\\cf");
            out.append(QByteArray::number(colorIndex(style.foreground) + 1));
        }
    }

    // Background/highlight color
    if (m_filter.includeHighlights) {
        if (style.background.isValid()) {
            out.append("\\highlight");
            out.append(QByteArray::number(colorIndex(style.background) + 1));
        }
    }

    out.append(" ");
}

void ContentRtfExporter::writeParagraphFormat(QByteArray &out, const Content::ParagraphFormat &fmt)
{
    // Alignment
    if (m_filter.includeAlignment) {
        if (fmt.alignment & Qt::AlignHCenter)
            out.append("\\qc");
        else if (fmt.alignment & Qt::AlignRight)
            out.append("\\qr");
        else if (fmt.alignment & Qt::AlignJustify)
            out.append("\\qj");
        else
            out.append("\\ql");
    }

    // Space before/after (in twips)
    if (m_filter.includeSpacing) {
        if (fmt.spaceBefore > 0) {
            out.append("\\sb");
            out.append(QByteArray::number(RtfUtils::toTwips(fmt.spaceBefore)));
        }
        if (fmt.spaceAfter > 0) {
            out.append("\\sa");
            out.append(QByteArray::number(RtfUtils::toTwips(fmt.spaceAfter)));
        }
    }

    // Margins
    if (m_filter.includeMargins) {
        if (fmt.leftMargin > 0) {
            out.append("\\li");
            out.append(QByteArray::number(RtfUtils::toTwips(fmt.leftMargin)));
        }
        if (fmt.rightMargin > 0) {
            out.append("\\ri");
            out.append(QByteArray::number(RtfUtils::toTwips(fmt.rightMargin)));
        }
        if (fmt.firstLineIndent > 0) {
            out.append("\\fi");
            out.append(QByteArray::number(RtfUtils::toTwips(fmt.firstLineIndent)));
        }
    }

    // Line spacing
    if (m_filter.includeSpacing) {
        if (fmt.lineHeightPercent > 100) {
            int spacing = qRound(240.0 * fmt.lineHeightPercent / 100.0);
            out.append("\\sl");
            out.append(QByteArray::number(spacing));
            out.append("\\slmult1");
        }
    }

    // Background
    if (m_filter.includeHighlights) {
        if (fmt.background.isValid()) {
            out.append("\\cbpat");
            out.append(QByteArray::number(colorIndex(fmt.background) + 1));
        }
    }
}

// --- Helpers ---

int ContentRtfExporter::fontIndex(const QString &family)
{
    auto it = m_fonts.find(family);
    if (it != m_fonts.end())
        return it.value();

    int idx = m_fonts.size();
    m_fonts.insert(family, idx);
    return idx;
}

int ContentRtfExporter::colorIndex(const QColor &color)
{
    QRgb rgb = color.rgb();
    auto it = m_colors.find(rgb);
    if (it != m_colors.end())
        return it.value();

    int idx = m_colors.size();
    m_colors.insert(rgb, idx);
    return idx;
}

