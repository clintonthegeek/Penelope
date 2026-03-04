#ifndef PENELOPE_RTFEXPORTER_H
#define PENELOPE_RTFEXPORTER_H

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QMap>
#include <QString>

#include "rtfutils.h"

class QTextDocument;
class QTextBlock;
class QTextCharFormat;
class QTextBlockFormat;
class QTextTable;

// Exports a QTextDocument to RTF format.
// Walks the document blocks and generates RTF with proper font table,
// color table, paragraph formatting, and character formatting.

class RtfExporter
{
public:
    RtfExporter();

    // Export the entire document to RTF
    QByteArray exportDocument(const QTextDocument *document);

    // Export document to a file. Returns true on success.
    bool exportToFile(const QTextDocument *document, const QString &filePath);

private:
    // Build font table from all fonts used in the document
    void buildFontTable(const QTextDocument *document);

    // Build color table from all colors used in the document
    void buildColorTable(const QTextDocument *document);

    // Get font index in the font table (adds if new)
    int fontIndex(const QString &family);

    // Get color index in the color table (adds if new)
    int colorIndex(const QColor &color);

    // Write RTF header (font table, color table, document defaults)
    void writeHeader(QByteArray &out);

    // Write a text block
    void writeBlock(QByteArray &out, const QTextBlock &block);

    // Write character formatting commands
    void writeCharFormat(QByteArray &out, const QTextCharFormat &fmt);

    // Write paragraph formatting commands
    void writeParaFormat(QByteArray &out, const QTextBlockFormat &fmt);

    // Write a table
    void writeTable(QByteArray &out, const QTextTable *table);

    QList<QString> m_fontTable;
    QMap<QString, int> m_fontMap;
    QList<QColor> m_colorTable;
    QMap<QRgb, int> m_colorMap;
};

#endif // PENELOPE_RTFEXPORTER_H
