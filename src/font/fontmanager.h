/*
 * fontmanager.h — Font loading, metrics, and subsetting
 *
 * Uses FreeType for glyph metrics, fontconfig for font resolution,
 * and HarfBuzz for shaping support.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_FONTMANAGER_H
#define PENELOPE_FONTMANAGER_H

#include <QHash>
#include <QRawFont>

#include <QSet>
#include <QString>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>

class HersheyFont;
class QPainterPath;

struct FontFace {
    FT_Face ftFace = nullptr;
    hb_font_t *hbFont = nullptr;
    QString filePath;
    int faceIndex = 0;
    QByteArray rawData; // kept alive for FreeType/HarfBuzz

    QSet<uint> usedGlyphs;

    // Hershey stroke font support
    bool isHershey = false;
    HersheyFont *hersheyFont = nullptr;
    bool hersheyBold = false;    // synthesize bold via stroke width
    bool hersheyItalic = false;  // synthesize italic via skew

    ~FontFace();
};

namespace sfnt { struct SubsetResult; }

class FontManager {
public:
    FontManager();
    FontManager(const FontManager &) = delete;
    FontManager &operator=(const FontManager &) = delete;
    ~FontManager();

    FontFace *loadFont(const QString &family, int weight = 400, bool italic = false);
    FontFace *loadFontFromPath(const QString &filePath, int faceIndex = 0);

    void markGlyphUsed(FontFace *face, uint glyphId);
    sfnt::SubsetResult subsetFont(FontFace *face) const;
    void resetUsage();

    // Metrics (all in points at the given size)
    qreal ascent(FontFace *face, qreal sizePoints) const;
    qreal descent(FontFace *face, qreal sizePoints) const;
    qreal lineHeight(FontFace *face, qreal sizePoints) const;
    qreal glyphWidth(FontFace *face, uint glyphId, qreal sizePoints) const;

    /// The width a PDF viewer will advance by for this glyph, in 1/1000 em:
    /// exactly the value writeCidFont puts in the /W array. Returns -1 when the
    /// glyph has no /W entry, in which case a viewer falls back to /DW and the
    /// value cannot be predicted. Run-length text operators must not be used
    /// for such a glyph.
    int pdfGlyphWidth(FontFace *face, uint glyphId) const;

    // Font info
    qreal unitsPerEm(FontFace *face) const;
    QByteArray rawFontData(FontFace *face) const;

    // One QRawFont per face and size, shared by every renderer that uses this
    // manager, so a document with many view items builds each raw font once.
    const QRawFont &rawFont(FontFace *face, qreal sizePoints);
    // Structural instrumentation for the tests.
    int rawFontCreations() const { return m_rawFontCreations; }
    QString postScriptName(FontFace *face) const;
    int fontFlags(FontFace *face) const;
    qreal capHeight(FontFace *face, qreal sizePoints) const;
    qreal italicAngle(FontFace *face) const;

    // BBox in PDF units (per 1000 units = 1 em)
    QList<int> fontBBox(FontFace *face) const;

private:
    struct FontKey {
        QString family;
        int weight;   // QFont::Weight enum values (400=Normal, 700=Bold, etc.)
        bool italic;

        bool operator==(const FontKey &o) const
        {
            return family == o.family && weight == o.weight && italic == o.italic;
        }
    };
    friend size_t qHash(const FontManager::FontKey &k, size_t seed);

    FT_Library m_ftLibrary = nullptr;
    QHash<FontKey, FontFace *> m_faces;
    QHash<QString, FontFace *> m_facesByPath;

    QString resolveFontPath(const QString &family, int weight, bool italic) const;

    QHash<QPair<FontFace *, int>, QRawFont> m_rawFonts; // key: (face, size * 100)
    int m_rawFontCreations = 0;
};

#endif // PENELOPE_FONTMANAGER_H
