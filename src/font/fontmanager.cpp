/*
 * fontmanager.cpp — Font loading, metrics, and subsetting
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fontmanager.h"
#include "hersheyfont.h"
#include "sfnt.h"

#include <QFile>
#include <QDebug>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_IDS_H
#include FT_TRUETYPE_TABLES_H

#include <hb.h>
#include <hb-ft.h>

#include <fontconfig/fontconfig.h>

size_t qHash(const FontManager::FontKey &k, size_t seed = 0)
{
    return qHash(k.family, seed) ^ qHash(k.weight, seed) ^ qHash(k.italic, seed);
}

FontFace::~FontFace()
{
    if (hbFont) {
        hb_font_destroy(hbFont);
        hbFont = nullptr;
    }
    if (ftFace) {
        FT_Done_Face(ftFace);
        ftFace = nullptr;
    }
}

FontManager::FontManager()
{
    FT_Error err = FT_Init_FreeType(&m_ftLibrary);
    if (err) {
        qWarning() << "FontManager: Failed to initialize FreeType:" << err;
        m_ftLibrary = nullptr;
    }
}

FontManager::~FontManager()
{
    // m_facesByPath is the canonical owner — each file path maps to exactly
    // one FontFace.  m_faces may have multiple keys aliased to the same
    // FontFace* (when fontconfig resolves different weight/style requests
    // to the same physical file), so deleting from m_faces would double-free.
    m_faces.clear();
    qDeleteAll(m_facesByPath);
    m_facesByPath.clear();
    if (m_ftLibrary) {
        FT_Done_FreeType(m_ftLibrary);
        m_ftLibrary = nullptr;
    }
}

QString FontManager::resolveFontPath(const QString &family, int weight, bool italic) const
{
    FcConfig *config = FcInitLoadConfigAndFonts();
    if (!config)
        return {};

    FcPattern *pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY,
                       reinterpret_cast<const FcChar8 *>(family.toUtf8().constData()));

    // Map QFont weight (0-1000) to fontconfig weight (0-210)
    int fcWeight = FC_WEIGHT_REGULAR;
    if (weight <= 100)
        fcWeight = FC_WEIGHT_THIN;
    else if (weight <= 200)
        fcWeight = FC_WEIGHT_EXTRALIGHT;
    else if (weight <= 300)
        fcWeight = FC_WEIGHT_LIGHT;
    else if (weight <= 400)
        fcWeight = FC_WEIGHT_REGULAR;
    else if (weight <= 500)
        fcWeight = FC_WEIGHT_MEDIUM;
    else if (weight <= 600)
        fcWeight = FC_WEIGHT_DEMIBOLD;
    else if (weight <= 700)
        fcWeight = FC_WEIGHT_BOLD;
    else if (weight <= 800)
        fcWeight = FC_WEIGHT_EXTRABOLD;
    else
        fcWeight = FC_WEIGHT_BLACK;
    FcPatternAddInteger(pat, FC_WEIGHT, fcWeight);

    FcPatternAddInteger(pat, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);

    FcConfigSubstitute(config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult fcResult;
    FcPattern *match = FcFontMatch(config, pat, &fcResult);
    QString path;
    if (match) {
        FcChar8 *file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file)
            path = QString::fromUtf8(reinterpret_cast<const char *>(file));
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);
    FcConfigDestroy(config);
    return path;
}

FontFace *FontManager::loadFont(const QString &family, int weight, bool italic)
{
    FontKey key{family, weight, italic};
    if (auto *existing = m_faces.value(key))
        return existing;

    // Hershey font routing
    if (family.startsWith(QLatin1String("Hershey "))) {
        HersheyFontRegistry::instance().ensureLoaded();
        HersheyFontResult hr = HersheyFontRegistry::instance().resolve(family, weight, italic);
        if (hr.font) {
            auto *face = new FontFace;
            face->isHershey = true;
            face->hersheyFont = hr.font;
            face->hersheyBold = hr.synthesizeBold;
            face->hersheyItalic = hr.synthesizeItalic;
            m_faces.insert(key, face);
            return face;
        }
        qWarning() << "FontManager: Could not resolve Hershey font:" << family << weight << italic;
        return nullptr;
    }

    QString path = resolveFontPath(family, weight, italic);
    if (path.isEmpty()) {
        qWarning() << "FontManager: Could not resolve font:" << family << weight << italic;
        return nullptr;
    }

    FontFace *face = loadFontFromPath(path, 0);
    if (face)
        m_faces.insert(key, face);
    return face;
}

FontFace *FontManager::loadFontFromPath(const QString &filePath, int faceIndex)
{
    QString cacheKey = filePath + QStringLiteral(":") + QString::number(faceIndex);
    if (auto *existing = m_facesByPath.value(cacheKey))
        return existing;

    if (!m_ftLibrary)
        return nullptr;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "FontManager: Cannot open font file:" << filePath;
        return nullptr;
    }

    auto *face = new FontFace;
    face->filePath = filePath;
    face->faceIndex = faceIndex;
    face->rawData = file.readAll();

    FT_Error err = FT_New_Memory_Face(
        m_ftLibrary,
        reinterpret_cast<const FT_Byte *>(face->rawData.constData()),
        face->rawData.size(),
        faceIndex,
        &face->ftFace);
    if (err) {
        qWarning() << "FontManager: FreeType failed to load:" << filePath << "error:" << err;
        delete face;
        return nullptr;
    }

    // Create HarfBuzz font from FreeType face
    face->hbFont = hb_ft_font_create_referenced(face->ftFace);
    if (!face->hbFont) {
        qWarning() << "FontManager: HarfBuzz font creation failed:" << filePath;
        delete face;
        return nullptr;
    }

    m_facesByPath.insert(cacheKey, face);
    return face;
}

void FontManager::markGlyphUsed(FontFace *face, uint glyphId)
{
    if (face)
        face->usedGlyphs.insert(glyphId);
}

sfnt::SubsetResult FontManager::subsetFont(FontFace *face) const
{
    if (!face || face->rawData.isEmpty())
        return {};

    QList<uint> glyphIds(face->usedGlyphs.begin(), face->usedGlyphs.end());
    return sfnt::subsetFace(face->rawData, glyphIds, face->faceIndex);
}

void FontManager::resetUsage()
{
    for (auto *face : m_faces)
        face->usedGlyphs.clear();
    for (auto *face : m_facesByPath)
        face->usedGlyphs.clear();
}

// --- Metrics ---

static qreal ftUnitsToPoints(FT_Face face, FT_Long units, qreal sizePoints)
{
    if (!face || face->units_per_EM == 0)
        return 0;
    return static_cast<qreal>(units) * sizePoints / face->units_per_EM;
}

qreal FontManager::ascent(FontFace *face, qreal sizePoints) const
{
    if (!face) return sizePoints;
    if (face->isHershey && face->hersheyFont) {
        qreal scale = sizePoints / face->hersheyFont->unitsPerEm();
        return face->hersheyFont->ascent() * scale;
    }
    if (!face->ftFace) return sizePoints;
    return ftUnitsToPoints(face->ftFace, face->ftFace->ascender, sizePoints);
}

qreal FontManager::descent(FontFace *face, qreal sizePoints) const
{
    if (!face) return 0;
    if (face->isHershey && face->hersheyFont) {
        qreal scale = sizePoints / face->hersheyFont->unitsPerEm();
        return face->hersheyFont->descent() * scale;
    }
    if (!face->ftFace) return 0;
    // FreeType descent is negative; return as positive
    return -ftUnitsToPoints(face->ftFace, face->ftFace->descender, sizePoints);
}

qreal FontManager::lineHeight(FontFace *face, qreal sizePoints) const
{
    if (!face) return sizePoints * 1.2;
    if (face->isHershey && face->hersheyFont) {
        return ascent(face, sizePoints) + descent(face, sizePoints);
    }
    if (!face->ftFace) return sizePoints * 1.2;
    return ftUnitsToPoints(face->ftFace, face->ftFace->height, sizePoints);
}

qreal FontManager::glyphWidth(FontFace *face, uint glyphId, qreal sizePoints) const
{
    if (!face) return 0;
    if (face->isHershey && face->hersheyFont) {
        qreal scale = sizePoints / face->hersheyFont->unitsPerEm();
        return face->hersheyFont->advanceWidth(static_cast<char32_t>(glyphId)) * scale;
    }
    if (!face->ftFace) return 0;
    FT_Set_Char_Size(face->ftFace, static_cast<FT_F26Dot6>(sizePoints * 64), 0, 72, 0);
    FT_Error err = FT_Load_Glyph(face->ftFace, glyphId, FT_LOAD_NO_BITMAP);
    if (err) return 0;
    return face->ftFace->glyph->advance.x / 64.0;
}

int FontManager::pdfGlyphWidth(FontFace *face, uint glyphId) const
{
    if (!face || face->isHershey)
        return -1;
    // Must mirror writeCidFont exactly: it only emits /W entries for glyphs in
    // usedGlyphs, and a viewer substitutes /DW for anything absent.
    if (!face->usedGlyphs.contains(glyphId))
        return -1;
    const qreal upem = unitsPerEm(face);
    if (upem <= 0)
        return -1;
    const qreal w = glyphWidth(face, glyphId, upem);
    if (w <= 0)
        return -1;
    return int(w * 1000.0 / upem);
}

qreal FontManager::unitsPerEm(FontFace *face) const
{
    if (!face || !face->ftFace) return 1000;
    return face->ftFace->units_per_EM;
}

QByteArray FontManager::rawFontData(FontFace *face) const
{
    if (!face) return {};
    return face->rawData;
}

const QRawFont &FontManager::rawFont(FontFace *face, qreal sizePoints)
{
    static const QRawFont empty;
    if (!face || sizePoints <= 0)
        return empty;
    const auto key = qMakePair(face, int(qRound(sizePoints * 100)));
    auto it = m_rawFonts.find(key);
    if (it != m_rawFonts.end())
        return it.value();

    QRawFont font(face->rawData, sizePoints);
    ++m_rawFontCreations;
    auto inserted = m_rawFonts.insert(key, font);
    return inserted.value();
}

QString FontManager::postScriptName(FontFace *face) const
{
    if (!face || !face->ftFace) return {};
    const char *psName = FT_Get_Postscript_Name(face->ftFace);
    return psName ? QString::fromLatin1(psName) : QStringLiteral("Unknown");
}

int FontManager::fontFlags(FontFace *face) const
{
    if (!face || !face->ftFace) return 0;
    int flags = 0;
    // PDF font flags (PDF32000-2008, Table 123)
    if (FT_IS_FIXED_WIDTH(face->ftFace))
        flags |= (1 << 0); // FixedPitch
    // Bit 1: Serif (heuristic: check OS/2 table)
    flags |= (1 << 5); // Nonsymbolic (always set for Identity-H)
    if (face->ftFace->style_flags & FT_STYLE_FLAG_ITALIC)
        flags |= (1 << 6); // Italic
    return flags;
}

qreal FontManager::capHeight(FontFace *face, qreal sizePoints) const
{
    if (!face || !face->ftFace) return sizePoints * 0.7;
    // Try OS/2 table first
    auto *os2 = reinterpret_cast<TT_OS2 *>(
        FT_Get_Sfnt_Table(face->ftFace, FT_SFNT_OS2));
    if (os2 && os2->sCapHeight > 0)
        return ftUnitsToPoints(face->ftFace, os2->sCapHeight, sizePoints);
    // Fallback: measure capital H
    FT_UInt gid = FT_Get_Char_Index(face->ftFace, 'H');
    if (gid) {
        FT_Set_Char_Size(face->ftFace, static_cast<FT_F26Dot6>(sizePoints * 64), 0, 72, 0);
        if (FT_Load_Glyph(face->ftFace, gid, FT_LOAD_NO_BITMAP) == 0)
            return face->ftFace->glyph->metrics.height / 64.0;
    }
    return sizePoints * 0.7;
}

qreal FontManager::italicAngle(FontFace *face) const
{
    if (!face || !face->ftFace) return 0;
    auto *post = reinterpret_cast<TT_Postscript *>(
        FT_Get_Sfnt_Table(face->ftFace, FT_SFNT_POST));
    if (post)
        return static_cast<qreal>(post->italicAngle) / 65536.0;
    return (face->ftFace->style_flags & FT_STYLE_FLAG_ITALIC) ? -12.0 : 0.0;
}

QList<int> FontManager::fontBBox(FontFace *face) const
{
    if (!face || !face->ftFace)
        return {0, 0, 1000, 1000};
    FT_BBox bbox = face->ftFace->bbox;
    int upem = face->ftFace->units_per_EM;
    if (upem == 0) upem = 1000;
    auto scale = [upem](FT_Pos v) { return static_cast<int>(v * 1000 / upem); };
    return {scale(bbox.xMin), scale(bbox.yMin), scale(bbox.xMax), scale(bbox.yMax)};
}
