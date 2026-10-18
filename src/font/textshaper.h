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
#include <functional>

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
    int cacheLimit() const { return m_cache.maxCost(); }
    void setCancellationCheck(std::function<bool()> check) { m_cancellationCheck = std::move(check); }
    quint64 cacheHits() const { return m_cacheHits; }
    quint64 cacheMisses() const { return m_cacheMisses; }
    int cacheCost() const { return m_cache.totalCost(); }

private:
    struct CachedShape { QList<ShapedRun> runs; };
    // A layout holds a paragraph's runs at a time, so a few megabytes cover a
    // long prose document while an entry larger than the budget (one enormous
    // fence line) is laid out and simply not stored.
    static constexpr int kDefaultCacheBytes = 8 * 1024 * 1024;
    QCache<QByteArray, CachedShape> m_cache{kDefaultCacheBytes};
    quint64 m_cacheHits = 0;
    quint64 m_cacheMisses = 0;
    std::function<bool()> m_cancellationCheck;
    QList<ShapedRun> shapeUncached(const QString &text, const QList<StyleRun> &styles);
    struct InternalRun {
        int start;
        int length;
        int dir; // 0=LTR, 1=RTL
        int script; // UScriptCode
        int styleIndex; // index into styles list
        bool useFallbackFont = false;
        FontFace *overrideFace = nullptr; // a face fontconfig found for this run
    };

    // The face that covers a character, in preference order: the primary face
    // itself, the bundled symbol fallback, then whatever the system offers.
    FontFace *faceForCharacter(FontFace *primary, char32_t codePoint) const;
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
