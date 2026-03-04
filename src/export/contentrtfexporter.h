/*
 * contentrtfexporter.h — RTF generation from Content:: model nodes
 *
 * Converts a list of Content::BlockNode into styled RTF suitable for
 * clipboard export. Used by DocumentView::copySelection() to produce
 * rich-text on copy so that pasting into Word/LibreOffice/Google Docs
 * preserves fonts, sizes, colors, bold/italic, code styling, etc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_CONTENTRTFEXPORTER_H
#define PENELOPE_CONTENTRTFEXPORTER_H

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QMap>
#include <QString>

#include "contentmodel.h"
#include "rtffilteroptions.h"
#include "rtfutils.h"

class ContentRtfExporter
{
public:
    QByteArray exportBlocks(const QList<Content::BlockNode> &blocks,
                            const RtfFilterOptions &filter = RtfFilterOptions());

private:
    // Pre-scan to collect fonts and colors
    void scanStyles(const QList<Content::BlockNode> &blocks);
    void scanInlines(const QList<Content::InlineNode> &inlines);
    void scanTextStyle(const Content::TextStyle &style);
    void scanParagraphFormat(const Content::ParagraphFormat &fmt);

    // RTF generation
    QByteArray writeHeader();
    void writeBlock(QByteArray &out, const Content::BlockNode &block);
    void writeParagraph(QByteArray &out, const Content::Paragraph &para);
    void writeHeading(QByteArray &out, const Content::Heading &heading);
    void writeCodeBlock(QByteArray &out, const Content::CodeBlock &cb);
    void writeList(QByteArray &out, const Content::List &list, int depth = 0);
    void writeTable(QByteArray &out, const Content::Table &table);
    void writeHorizontalRule(QByteArray &out);
    void writeBlockQuote(QByteArray &out, const Content::BlockQuote &bq);
    void writeInlines(QByteArray &out, const QList<Content::InlineNode> &inlines,
                      const QColor &foregroundOverride = QColor());
    void writeCharFormat(QByteArray &out, const Content::TextStyle &style);
    void writeParagraphFormat(QByteArray &out, const Content::ParagraphFormat &fmt);

    // Code block syntax highlighting
    QList<Content::InlineNode> buildCodeInlines(const Content::CodeBlock &cb);

    // Helpers
    int fontIndex(const QString &family);
    int colorIndex(const QColor &color);

    QMap<QString, int> m_fonts;
    QMap<QRgb, int> m_colors;
    RtfFilterOptions m_filter;
};

#endif // PENELOPE_CONTENTRTFEXPORTER_H
