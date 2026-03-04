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
#include <QObject>
#include <QSet>
#include <QString>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>

class HersheyFont;

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

class FontManager : public QObject {
    Q_OBJECT
public:
    explicit FontManager(QObject *parent = nullptr);
    ~FontManager() override;

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

    // Font info
    qreal unitsPerEm(FontFace *face) const;
    QByteArray rawFontData(FontFace *face) const;
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
};

#endif // PENELOPE_FONTMANAGER_H
