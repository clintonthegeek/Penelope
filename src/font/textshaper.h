/*
 * textshaper.h — BiDi + script itemization + HarfBuzz shaping
 *
 * Adapted from Scribus text/textshaper.h. Rewritten for our content model.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_TEXTSHAPER_H
#define PENELOPE_TEXTSHAPER_H

#include <QList>
#include <QCache>
#include <QString>
#include <QStringList>

#include <hb.h>

class FontManager;
struct FontFace;

struct ShapedGlyph {
    uint glyphId = 0;
    qreal xAdvance = 0;
    qreal yAdvance = 0;
    qreal xOffset = 0;
    qreal yOffset = 0;
    int cluster = 0; // character index in source text
};

struct ShapedRun {
    QList<ShapedGlyph> glyphs;
    FontFace *font = nullptr;
    qreal fontSize = 0;
    int textStart = 0;    // start index in original text
    int textLength = 0;
    bool rtl = false;
};

struct StyleRun {
    int start = 0;
    int length = 0;
    QString fontFamily;
    int fontWeight = 400;
    bool fontItalic = false;
    qreal fontSize = 11.0;
    QStringList fontFeatures; // e.g. "liga", "smcp", "-liga"
};

class TextShaper {
public:
    explicit TextShaper(FontManager *fontManager);

    QList<ShapedRun> shape(const QString &text, const QList<StyleRun> &styles);

    void setFallbackFont(FontFace *face) { m_fallbackFont = face; m_cache.clear(); }
    void setCacheLimit(int bytes) { m_cache.setMaxCost(qMax(0, bytes)); }
    quint64 cacheHits() const { return m_cacheHits; }
    quint64 cacheMisses() const { return m_cacheMisses; }
    int cacheCost() const { return m_cache.totalCost(); }

private:
    struct CachedShape { QList<ShapedRun> runs; };
    QCache<QByteArray, CachedShape> m_cache{8 * 1024 * 1024};
    quint64 m_cacheHits = 0;
    quint64 m_cacheMisses = 0;
    QList<ShapedRun> shapeUncached(const QString &text, const QList<StyleRun> &styles);
    struct InternalRun {
        int start;
        int length;
        int dir; // 0=LTR, 1=RTL
        int script; // UScriptCode
        int styleIndex; // index into styles list
        bool useFallbackFont = false;
    };

    QList<InternalRun> itemizeBiDi(const QString &text) const;
    QList<InternalRun> itemizeScripts(const QString &text,
                                      const QList<InternalRun> &runs) const;
    QList<InternalRun> itemizeStyles(const QList<InternalRun> &runs,
                                     const QList<StyleRun> &styles) const;
    QList<InternalRun> itemizeFontCoverage(const QString &text,
                                           const QList<InternalRun> &runs,
                                           const QList<StyleRun> &styles) const;

    FontManager *m_fontManager;
    FontFace *m_fallbackFont = nullptr;
};

#endif // PENELOPE_TEXTSHAPER_H
