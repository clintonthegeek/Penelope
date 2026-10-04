/*
 * hersheyfont.cpp — Hershey vector font JHF parser and registry
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hersheyfont.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <cmath>
#include <limits>

// ===========================================================================
// HersheyFont
// ===========================================================================

bool HersheyFont::load(const QString &resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "HersheyFont: cannot open" << resourcePath;
        return false;
    }

    m_name = QFileInfo(resourcePath).baseName();
    m_glyphs.clear();

    // JHF: each glyph is one logical line. A glyph starts when columns 0-4
    // contain a right-justified integer. Continuation lines start with spaces
    // and no ID — their data is appended to the previous glyph.
    char32_t codepoint = 32; // ASCII printable starts at space
    QByteArray accumulated;

    while (!file.atEnd()) {
        // Only strip trailing whitespace (newlines, CR, spaces) — leading
        // spaces are significant because glyph IDs are right-justified
        // in the first 5 columns.
        QByteArray rawLine = file.readLine();
        while (!rawLine.isEmpty() && (rawLine.back() == '\n' || rawLine.back() == '\r'
                                       || rawLine.back() == ' '))
            rawLine.chop(1);
        if (rawLine.isEmpty())
            continue;

        // Check whether this is a new glyph (columns 0-4 contain a number)
        // or a continuation line.
        const QByteArray prefix = rawLine.left(5);
        bool isNewGlyph = false;
        for (char ch : prefix) {
            if (ch >= '0' && ch <= '9') {
                isNewGlyph = true;
                break;
            }
        }

        if (isNewGlyph) {
            // Flush previously accumulated glyph
            if (!accumulated.isEmpty()) {
                parseGlyphLine(accumulated, codepoint);
                ++codepoint;
            }
            accumulated = rawLine;
        } else {
            // Continuation line — append coordinate data
            accumulated.append(rawLine);
        }
    }

    // Flush the last glyph
    if (!accumulated.isEmpty()) {
        parseGlyphLine(accumulated, codepoint);
    }

    // Alias non-breaking space (U+00A0) to regular space if not already present.
    // ShortWords typography replaces spaces after short words (a, the, in, ...)
    // with NBSP.  Without this alias the Hershey font reports hasGlyph = false
    // for NBSP, causing font-coverage splits that strip inter-word spacing.
    if (m_glyphs.contains(U' ') && !m_glyphs.contains(U'\u00A0'))
        m_glyphs.insert(U'\u00A0', m_glyphs.value(U' '));

    computeMetrics();
    return !m_glyphs.isEmpty();
}

const HersheyGlyph *HersheyFont::glyph(char32_t codepoint) const
{
    auto it = m_glyphs.constFind(codepoint);
    return (it != m_glyphs.constEnd()) ? &it.value() : nullptr;
}

bool HersheyFont::hasGlyph(char32_t codepoint) const
{
    return m_glyphs.contains(codepoint);
}

int HersheyFont::advanceWidth(char32_t codepoint) const
{
    auto it = m_glyphs.constFind(codepoint);
    if (it == m_glyphs.constEnd())
        return 0;
    return it->rightBound - it->leftBound;
}

void HersheyFont::parseGlyphLine(const QByteArray &line, char32_t codepoint)
{
    // JHF format:
    //   Positions 0-4:  5-char glyph ID (right-justified, space-padded)
    //   Positions 5-7:  3-char vertex count (includes the boundary pair)
    //   Position  8:    Left boundary character
    //   Position  9:    Right boundary character
    //   Positions 10+:  Coordinate pairs (2 chars each)
    //
    // Coordinate decoding: value = char_ascii_value - 'R' (ASCII 82)
    // Pen-up marker: " R" (space + 'R') means lift pen — start new stroke.
    // Y-axis is inverted: store QPointF(x, -y).

    if (line.size() < 10) {
        // Too short to contain even boundary info
        return;
    }

    HersheyGlyph g;

    // Decode boundary pair
    g.leftBound = static_cast<int>(line.at(8)) - 'R';
    g.rightBound = static_cast<int>(line.at(9)) - 'R';

    // Parse coordinate pairs from position 10 onward
    const int dataLen = line.size();
    QVector<QPointF> currentStroke;

    for (int i = 10; i + 1 < dataLen; i += 2) {
        const char c1 = line.at(i);
        const char c2 = line.at(i + 1);

        // Pen-up marker: space followed by 'R'
        if (c1 == ' ' && c2 == 'R') {
            if (!currentStroke.isEmpty()) {
                g.strokes.append(currentStroke);
                currentStroke.clear();
            }
            continue;
        }

        const int x = static_cast<int>(c1) - 'R';
        const int y = static_cast<int>(c2) - 'R';
        currentStroke.append(QPointF(x, -y));
    }

    // Flush last stroke
    if (!currentStroke.isEmpty()) {
        g.strokes.append(currentStroke);
    }

    m_glyphs.insert(codepoint, g);
}

void HersheyFont::computeMetrics()
{
    // Scan all glyph vertices to find the max ascent (max Y, since we
    // flipped Y) and max descent (min Y after flip).
    double maxY = -std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();

    for (auto it = m_glyphs.constBegin(); it != m_glyphs.constEnd(); ++it) {
        for (const auto &stroke : it->strokes) {
            for (const QPointF &pt : stroke) {
                if (pt.y() > maxY) maxY = pt.y();
                if (pt.y() < minY) minY = pt.y();
            }
        }
    }

    if (maxY <= minY) {
        // No stroke data at all
        m_ascent = 0;
        m_descent = 0;
        m_unitsPerEm = 1; // avoid divide by zero
        return;
    }

    // Ascent = highest point above baseline (positive Y after our flip)
    // Descent = deepest point below baseline (negative Y after flip,
    //           stored as positive magnitude)
    m_ascent = static_cast<int>(std::ceil(maxY));
    m_descent = static_cast<int>(std::ceil(-minY)); // minY is negative
    m_unitsPerEm = m_ascent + m_descent;

}

// ===========================================================================
// HersheyFontRegistry
// ===========================================================================

HersheyFontRegistry &HersheyFontRegistry::instance()
{
    static HersheyFontRegistry s_instance;
    return s_instance;
}

void HersheyFontRegistry::ensureLoaded()
{
    std::call_once(m_loadOnce, [this] {

    // Load all .jhf files from the Qt resource bundle
    QDirIterator it(QStringLiteral(":/hershey"), {QStringLiteral("*.jhf")},
                    QDir::Files);
    while (it.hasNext()) {
        const QString path = it.next();
        auto *font = new HersheyFont;
        if (font->load(path)) {
            m_fonts.insert(font->name(), font);
        } else {
            delete font;
        }
    }

    // -------------------------------------------------------------------
    // Family mapping table
    //   normal / bold / italic / boldItalic
    //   Empty string = no native variant (synthesize)
    // -------------------------------------------------------------------
    auto addFamily = [this](const QString &family,
                            const QString &normal,
                            const QString &bold,
                            const QString &italic,
                            const QString &boldItalic) {
        m_families.insert(family, FamilyEntry{normal, bold, italic, boldItalic});
    };

    addFamily(QStringLiteral("Hershey Sans"),
              QStringLiteral("futural"),
              QStringLiteral("futuram"),
              QString(), QString());

    addFamily(QStringLiteral("Hershey Roman"),
              QStringLiteral("rowmans"),
              QStringLiteral("rowmant"),
              QString(), QString());

    addFamily(QStringLiteral("Hershey Serif"),
              QStringLiteral("timesr"),
              QStringLiteral("timesrb"),
              QStringLiteral("timesi"),
              QStringLiteral("timesib"));

    addFamily(QStringLiteral("Hershey Script"),
              QStringLiteral("scripts"),
              QStringLiteral("scriptc"),
              QString(), QString());

    addFamily(QStringLiteral("Hershey Gothic English"),
              QStringLiteral("gothiceng"),
              QString(), QString(), QString());

    addFamily(QStringLiteral("Hershey Gothic German"),
              QStringLiteral("gothicger"),
              QStringLiteral("gothgbt"),
              QString(), QString());

    addFamily(QStringLiteral("Hershey Gothic Italian"),
              QStringLiteral("gothicita"),
              QStringLiteral("gothitt"),
              QString(), QString());

    addFamily(QStringLiteral("Hershey Greek"),
              QStringLiteral("greek"),
              QStringLiteral("greekc"),
              QStringLiteral("greeks"),
              QString());

    addFamily(QStringLiteral("Hershey Cyrillic"),
              QStringLiteral("cyrillic"),
              QStringLiteral("cyrilc_1"),
              QString(), QString());
    });
}

HersheyFontResult HersheyFontRegistry::resolve(const QString &family,
                                                 int weight,
                                                 bool italic) const
{
    const_cast<HersheyFontRegistry *>(this)->ensureLoaded();
    HersheyFontResult result;

    auto famIt = m_families.constFind(family);
    if (famIt == m_families.constEnd())
        return result; // unknown family

    const FamilyEntry &entry = famIt.value();
    const bool wantBold = (weight >= 600); // semi-bold and above

    // Try to find the best native variant
    QString fontName;
    bool needSynthBold = false;
    bool needSynthItalic = false;

    if (wantBold && italic) {
        // Bold italic
        fontName = entry.boldItalic;
        if (fontName.isEmpty()) {
            // Fall back to bold (synthesize italic)
            fontName = entry.bold;
            needSynthItalic = true;
            if (fontName.isEmpty()) {
                // Fall back to italic (synthesize bold)
                fontName = entry.italic;
                needSynthBold = true;
                needSynthItalic = false;
                if (fontName.isEmpty()) {
                    // Fall back to normal (synthesize both)
                    fontName = entry.normal;
                    needSynthBold = true;
                    needSynthItalic = true;
                }
            }
        }
    } else if (wantBold) {
        // Bold
        fontName = entry.bold;
        if (fontName.isEmpty()) {
            fontName = entry.normal;
            needSynthBold = true;
        }
    } else if (italic) {
        // Italic
        fontName = entry.italic;
        if (fontName.isEmpty()) {
            fontName = entry.normal;
            needSynthItalic = true;
        }
    } else {
        // Normal
        fontName = entry.normal;
    }

    if (!fontName.isEmpty()) {
        result.font = m_fonts.value(fontName, nullptr);
        result.synthesizeBold = needSynthBold;
        result.synthesizeItalic = needSynthItalic;
    }

    return result;
}

QStringList HersheyFontRegistry::familyNames() const
{
    const_cast<HersheyFontRegistry *>(this)->ensureLoaded();
    QStringList names = m_families.keys();
    names.sort();
    return names;
}
