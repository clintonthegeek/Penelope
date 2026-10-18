#include "contentplaintext.h"

namespace Content {
namespace {
QString inlineText(const QList<InlineNode> &inlines, const FootnoteStyle &noteStyle)
{
    QString text;
    for (const auto &node : inlines) {
        std::visit([&](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, TextRun> || std::is_same_v<T, InlineCode>
                          || std::is_same_v<T, Link>) text += n.text;
            else if constexpr (std::is_same_v<T, InlineImage>) text += n.altText;
            else if constexpr (std::is_same_v<T, Math>) text += n.source;
            else if constexpr (std::is_same_v<T, FootnoteRef>) text += footnoteMarker(noteStyle, n.index);
            else if constexpr (std::is_same_v<T, SoftBreak>) text += QLatin1Char(' ');
            else if constexpr (std::is_same_v<T, HardBreak>) text += QLatin1Char('\n');
        }, node);
    }
    return text;
}
QString blocksText(const QList<BlockNode> &blocks, const FootnoteStyle &noteStyle)
{
    QStringList parts;
    for (const auto &block : blocks) {
        std::visit([&](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Paragraph> || std::is_same_v<T, Heading>)
                parts.append(inlineText(n.inlines, noteStyle));
            else if constexpr (std::is_same_v<T, CodeBlock>) {
                QString code = n.code;
                // Parser includes the fenced block's final line terminator.
                if (code.endsWith(QLatin1Char('\n'))) code.chop(1);
                parts.append(code);
            } else if constexpr (std::is_same_v<T, BlockQuote>) parts.append(blocksText(n.children, noteStyle));
            else if constexpr (std::is_same_v<T, List>) {
                for (const auto &item : n.items) parts.append(blocksText(item.children, noteStyle));
            } else if constexpr (std::is_same_v<T, Table>) {
                for (const auto &row : n.rows) {
                    QStringList cells;
                    for (const auto &cell : row.cells) cells.append(inlineText(cell.inlines, noteStyle));
                    parts.append(cells.join(QLatin1Char('\t')));
                }
            } else if constexpr (std::is_same_v<T, FootnoteSection>) {
                for (const auto &note : n.footnotes)
                    parts.append(footnoteMarker(noteStyle, note.index) + QStringLiteral(". ")
                                 + blocksText(note.blocks, noteStyle));
            }
        }, block);
    }
    return parts.join(QLatin1Char('\n'));
}
}
QString toPlainText(const Document &document)
{
    QString text = blocksText(document.blocks, document.footnoteStyle);
    text.remove(QChar(0x00AD));
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    return text;
}
}
