/*
 * hersheyfont.h — Hershey vector font parser and registry
 *
 * Parses JHF (Jim Herd Font) files containing Hershey stroke fonts
 * and provides a registry for mapping CSS-like font families to
 * the appropriate Hershey font variant.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_HERSHEYFONT_H
#define PENELOPE_HERSHEYFONT_H

#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

// ---------------------------------------------------------------------------
// HersheyGlyph — a single glyph as a list of stroked polylines
// ---------------------------------------------------------------------------

struct HersheyGlyph {
    int leftBound = 0;
    int rightBound = 0;
    QVector<QVector<QPointF>> strokes; // list of polylines (pen-down segments)
};

// ---------------------------------------------------------------------------
// HersheyFont — loads and stores one .jhf font file
// ---------------------------------------------------------------------------

class HersheyFont
{
public:
    HersheyFont() = default;

    /// Load a JHF font from a Qt resource path (e.g. ":/hershey/futural.jhf").
    bool load(const QString &resourcePath);

    /// Return the glyph for a codepoint, or nullptr if not present.
    const HersheyGlyph *glyph(char32_t codepoint) const;

    /// Whether the font contains a glyph for the given codepoint.
    bool hasGlyph(char32_t codepoint) const;

    /// Advance width for a codepoint (rightBound - leftBound), or 0 if absent.
    int advanceWidth(char32_t codepoint) const;

    /// Font-wide ascent (positive, in Hershey coordinate units).
    int ascent() const { return m_ascent; }

    /// Font-wide descent (positive magnitude, in Hershey coordinate units).
    int descent() const { return m_descent; }

    /// Units per em (ascent + descent).
    int unitsPerEm() const { return m_unitsPerEm; }

    /// The base name of the loaded font (e.g. "futural").
    const QString &name() const { return m_name; }

private:
    /// Parse a single complete glyph line (after continuation joining).
    void parseGlyphLine(const QByteArray &line, char32_t codepoint);

    /// Scan all loaded glyphs to compute ascent / descent / unitsPerEm.
    void computeMetrics();

    QHash<char32_t, HersheyGlyph> m_glyphs;
    int m_ascent = 0;
    int m_descent = 0;
    int m_unitsPerEm = 0;
    QString m_name;
};

// ---------------------------------------------------------------------------
// HersheyFontResult — returned by the registry's resolve()
// ---------------------------------------------------------------------------

struct HersheyFontResult {
    HersheyFont *font = nullptr;
    bool synthesizeBold = false;
    bool synthesizeItalic = false;
};

// ---------------------------------------------------------------------------
// HersheyFontRegistry — singleton mapping family+weight+style to fonts
// ---------------------------------------------------------------------------

class HersheyFontRegistry
{
public:
    static HersheyFontRegistry &instance();

    /// Lazy, idempotent — loads all .jhf files from :/hershey/ on first call.
    void ensureLoaded();

    /// Resolve a CSS-style family/weight/italic request to a Hershey font,
    /// setting synthesis flags when no native variant exists.
    HersheyFontResult resolve(const QString &family, int weight, bool italic) const;

    /// List of all known Hershey family names.
    QStringList familyNames() const;

private:
    HersheyFontRegistry() = default;
    HersheyFontRegistry(const HersheyFontRegistry &) = delete;
    HersheyFontRegistry &operator=(const HersheyFontRegistry &) = delete;

    /// Internal entry in the family map.
    struct FamilyEntry {
        QString normal;
        QString bold;
        QString italic;
        QString boldItalic;
    };

    bool m_loaded = false;
    QHash<QString, HersheyFont *> m_fonts;           // name → font
    QHash<QString, FamilyEntry> m_families;           // family → entry
};

// ---------------------------------------------------------------------------
// HersheyConstants — named constants for Hershey font rendering
// ---------------------------------------------------------------------------

namespace HersheyConstants {
    constexpr qreal kItalicSkew = 0.2126;
    constexpr qreal kStrokeWidthFactor = 0.02;
    constexpr qreal kBoldStrokeMultiplier = 1.8;
}

#endif // PENELOPE_HERSHEYFONT_H
