/*
 * textshaper.cpp — BiDi + script itemization + HarfBuzz shaping
 *
 * Adapted from Scribus text/textshaper.cpp. Rewritten for our content model.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "textshaper.h"
#include "fontmanager.h"
#include "hersheyfont.h"

#include <hb.h>
#include <hb-ft.h>
#include <hb-icu.h>

#include <unicode/ubidi.h>
#include <unicode/uscript.h>
#include <unicode/uchar.h>

#include <QDebug>
#include <QDataStream>
#include <QIODevice>

TextShaper::TextShaper(FontManager *fontManager)
    : m_fontManager(fontManager)
{
}

// --- BiDi itemization using ICU ---

QList<TextShaper::InternalRun> TextShaper::itemizeBiDi(const QString &text) const
{
    QList<InternalRun> runs;
    if (text.isEmpty())
        return runs;

    UErrorCode err = U_ZERO_ERROR;
    UBiDi *bidi = ubidi_open();
    ubidi_setPara(bidi, reinterpret_cast<const UChar *>(text.utf16()),
                  text.length(), UBIDI_DEFAULT_LTR, nullptr, &err);

    if (U_SUCCESS(err)) {
        int32_t count = ubidi_countRuns(bidi, &err);
        if (U_SUCCESS(err)) {
            runs.reserve(count);
            for (int32_t i = 0; i < count; ++i) {
                int32_t start = 0, length = 0;
                UBiDiDirection dir = ubidi_getVisualRun(bidi, i, &start, &length);
                InternalRun run;
                run.start = start;
                run.length = length;
                run.dir = (dir == UBIDI_RTL) ? 1 : 0;
                run.script = USCRIPT_COMMON;
                run.styleIndex = 0;
                runs.append(run);
            }
        }
    }

    ubidi_close(bidi);
    return runs;
}

// --- Script itemization using ICU uscript_getScript ---

QList<TextShaper::InternalRun> TextShaper::itemizeScripts(
    const QString &text, const QList<InternalRun> &runs) const
{
    QList<InternalRun> result;

    for (const InternalRun &run : runs) {
        int pos = run.start;
        int end = run.start + run.length;

        while (pos < end) {
            UErrorCode err = U_ZERO_ERROR;
            UScriptCode script = uscript_getScript(text.at(pos).unicode(), &err);
            if (script == USCRIPT_COMMON || script == USCRIPT_INHERITED)
                script = USCRIPT_LATIN; // default

            int segStart = pos;
            while (pos < end) {
                UErrorCode err2 = U_ZERO_ERROR;
                UScriptCode sc = uscript_getScript(text.at(pos).unicode(), &err2);
                if (sc != USCRIPT_COMMON && sc != USCRIPT_INHERITED && sc != script)
                    break;
                if (sc != USCRIPT_COMMON && sc != USCRIPT_INHERITED)
                    script = sc;
                ++pos;
            }

            InternalRun sub;
            sub.start = segStart;
            sub.length = pos - segStart;
            sub.dir = run.dir;
            sub.script = script;
            sub.styleIndex = run.styleIndex;
            result.append(sub);
        }
    }

    return result;
}

// --- Style itemization: split runs at style boundaries ---

QList<TextShaper::InternalRun> TextShaper::itemizeStyles(
    const QList<InternalRun> &runs, const QList<StyleRun> &styles) const
{
    QList<InternalRun> result;
    if (styles.isEmpty())
        return runs;

    for (const InternalRun &run : runs) {
        int pos = run.start;
        int end = run.start + run.length;

        for (int si = 0; si < styles.size() && pos < end; ++si) {
            const StyleRun &style = styles[si];
            int styleEnd = style.start + style.length;

            if (styleEnd <= pos)
                continue;
            if (style.start >= end)
                break;

            int segStart = qMax(pos, style.start);
            int segEnd = qMin(end, styleEnd);
            if (segEnd <= segStart)
                continue;

            InternalRun sub;
            sub.start = segStart;
            sub.length = segEnd - segStart;
            sub.dir = run.dir;
            sub.script = run.script;
            sub.styleIndex = si;
            result.append(sub);
            pos = segEnd;
        }
    }

    return result;
}

// --- Font coverage itemization: split runs at primary/fallback boundaries ---

QList<TextShaper::InternalRun> TextShaper::itemizeFontCoverage(
    const QString &text, const QList<InternalRun> &runs,
    const QList<StyleRun> &styles) const
{
    if (!m_fallbackFont || !m_fallbackFont->ftFace)
        return runs;

    QList<InternalRun> result;

    for (const InternalRun &run : runs) {
        if (run.styleIndex < 0 || run.styleIndex >= styles.size()) {
            result.append(run);
            continue;
        }
        const StyleRun &style = styles[run.styleIndex];

        FontFace *primary = m_fontManager->loadFont(
            style.fontFamily, style.fontWeight, style.fontItalic);
        if (!primary) {
            result.append(run);
            continue;
        }

        // Hershey font coverage check
        if (primary->isHershey && primary->hersheyFont) {
            int pos = run.start;
            int end = run.start + run.length;
            while (pos < end) {
                // Decode codepoint (handle surrogate pairs)
                char32_t cp;
                if (pos + 1 < end
                    && QChar::isHighSurrogate(text.at(pos).unicode())
                    && QChar::isLowSurrogate(text.at(pos + 1).unicode())) {
                    cp = QChar::surrogateToUcs4(text.at(pos), text.at(pos + 1));
                } else {
                    cp = text.at(pos).unicode();
                }
                bool primaryHas = primary->hersheyFont->hasGlyph(cp);
                bool useFallback = !primaryHas
                    && m_fallbackFont && m_fallbackFont->ftFace
                    && FT_Get_Char_Index(m_fallbackFont->ftFace, cp) != 0;

                int segStart = pos;
                pos += (cp > 0xFFFF) ? 2 : 1;

                // Consume consecutive chars with same font choice
                while (pos < end) {
                    char32_t cp2;
                    if (pos + 1 < end
                        && QChar::isHighSurrogate(text.at(pos).unicode())
                        && QChar::isLowSurrogate(text.at(pos + 1).unicode())) {
                        cp2 = QChar::surrogateToUcs4(text.at(pos), text.at(pos + 1));
                    } else {
                        cp2 = text.at(pos).unicode();
                    }
                    bool p2 = primary->hersheyFont->hasGlyph(cp2);
                    bool f2 = !p2 && m_fallbackFont && m_fallbackFont->ftFace
                        && FT_Get_Char_Index(m_fallbackFont->ftFace, cp2) != 0;
                    if (f2 != useFallback)
                        break;
                    pos += (cp2 > 0xFFFF) ? 2 : 1;
                }

                InternalRun sub;
                sub.start = segStart;
                sub.length = pos - segStart;
                sub.dir = run.dir;
                sub.script = run.script;
                sub.styleIndex = run.styleIndex;
                sub.useFallbackFont = useFallback;
                result.append(sub);
            }
            continue; // skip FreeType coverage path
        }

        // Existing FreeType coverage path continues...
        if (!primary->ftFace) {
            result.append(run);
            continue;
        }

        int pos = run.start;
        int end = run.start + run.length;

        while (pos < end) {
            // Decode codepoint (handle surrogate pairs)
            uint cp;
            if (pos + 1 < end
                && QChar::isHighSurrogate(text.at(pos).unicode())
                && QChar::isLowSurrogate(text.at(pos + 1).unicode())) {
                cp = QChar::surrogateToUcs4(text.at(pos), text.at(pos + 1));
            } else {
                cp = text.at(pos).unicode();
            }

            bool primaryHas = FT_Get_Char_Index(primary->ftFace, cp) != 0;
            bool useFallback = !primaryHas
                && FT_Get_Char_Index(m_fallbackFont->ftFace, cp) != 0;

            int segStart = pos;
            // Advance past first character
            pos += (cp > 0xFFFF) ? 2 : 1;

            // Consume consecutive characters with the same font choice
            while (pos < end) {
                uint cp2;
                if (pos + 1 < end
                    && QChar::isHighSurrogate(text.at(pos).unicode())
                    && QChar::isLowSurrogate(text.at(pos + 1).unicode())) {
                    cp2 = QChar::surrogateToUcs4(text.at(pos), text.at(pos + 1));
                } else {
                    cp2 = text.at(pos).unicode();
                }

                bool p2 = FT_Get_Char_Index(primary->ftFace, cp2) != 0;
                bool f2 = !p2
                    && FT_Get_Char_Index(m_fallbackFont->ftFace, cp2) != 0;

                if (f2 != useFallback)
                    break;

                pos += (cp2 > 0xFFFF) ? 2 : 1;
            }

            InternalRun sub;
            sub.start = segStart;
            sub.length = pos - segStart;
            sub.dir = run.dir;
            sub.script = run.script;
            sub.styleIndex = run.styleIndex;
            sub.useFallbackFont = useFallback;
            result.append(sub);
        }
    }

    return result;
}

// --- Main shaping entry point ---

QList<ShapedRun> TextShaper::shape(const QString &text, const QList<StyleRun> &styles)
{
    if (m_cache.maxCost() == 0) { ++m_cacheMisses; return shapeUncached(text, styles); }
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << text << quint64(quintptr(m_fallbackFont)) << quint32(styles.size());
    for (const auto &style : styles)
        stream << style.start << style.length << style.fontFamily << style.fontWeight
               << style.fontItalic << style.fontSize << style.fontFeatures;
    if (auto *cached = m_cache.object(key)) {
        ++m_cacheHits;
        // resetUsage() is independent of cache lifetime. Replaying the glyphs
        // is essential: otherwise the next PDF contains an empty font subset.
        for (const auto &run : cached->runs)
            for (const auto &glyph : run.glyphs)
                m_fontManager->markGlyphUsed(run.font, glyph.glyphId);
        return cached->runs;
    }
    ++m_cacheMisses;
    auto runs = shapeUncached(text, styles);
    qint64 cost = key.size() + sizeof(CachedShape);
    for (const auto &run : runs)
        cost += sizeof(ShapedRun) + run.glyphs.size() * sizeof(ShapedGlyph);
    bool cancelled = m_cancellationCheck && m_cancellationCheck();
    if (!cancelled && !runs.isEmpty() && cost <= m_cache.maxCost())
        m_cache.insert(key, new CachedShape{runs}, int(cost));
    return runs;
}

QList<ShapedRun> TextShaper::shapeUncached(const QString &text, const QList<StyleRun> &styles)
{
    QList<ShapedRun> result;
    if (text.isEmpty() || styles.isEmpty())
        return result;

    // Pipeline: BiDi → Script → Style → Font-coverage itemization
    QList<InternalRun> bidiRuns = itemizeBiDi(text);
    QList<InternalRun> scriptRuns = itemizeScripts(text, bidiRuns);
    QList<InternalRun> styledRuns = itemizeStyles(scriptRuns, styles);
    QList<InternalRun> textRuns = itemizeFontCoverage(text, styledRuns, styles);

    for (const InternalRun &run : textRuns) {
        if (m_cancellationCheck && m_cancellationCheck()) {
            if (!result.isEmpty())
                return {};
            return result;
        }
        if (run.styleIndex < 0 || run.styleIndex >= styles.size())
            continue;
        const StyleRun &style = styles[run.styleIndex];

        // Load font (use fallback if coverage itemization flagged this run)
        FontFace *face;
        if (run.useFallbackFont && m_fallbackFont) {
            face = m_fallbackFont;
        } else {
            face = m_fontManager->loadFont(
                style.fontFamily, style.fontWeight, style.fontItalic);
        }
        if (!face)
            continue;

        // Hershey font: simple 1:1 character→glyph mapping, bypass HarfBuzz
        if (face->isHershey && face->hersheyFont) {
            ShapedRun shaped;
            shaped.font = face;
            shaped.fontSize = style.fontSize;
            shaped.textStart = run.start;
            shaped.textLength = run.length;
            shaped.rtl = false; // Hershey fonts are LTR only

            qreal scale = style.fontSize / face->hersheyFont->unitsPerEm();

            for (int ci = run.start; ci < run.start + run.length; ++ci) {
                char32_t cp = text.at(ci).unicode();
                // Handle surrogate pairs
                if (ci + 1 < run.start + run.length
                    && QChar::isHighSurrogate(text.at(ci).unicode())
                    && QChar::isLowSurrogate(text.at(ci + 1).unicode())) {
                    cp = QChar::surrogateToUcs4(text.at(ci), text.at(ci + 1));
                }

                ShapedGlyph g;
                g.glyphId = static_cast<uint>(cp); // glyphId == codepoint for Hershey
                g.xAdvance = face->hersheyFont->advanceWidth(cp) * scale;
                g.yAdvance = 0;
                g.xOffset = 0;
                g.yOffset = 0;
                g.cluster = ci;
                shaped.glyphs.append(g);

                // Skip low surrogate of a pair
                if (cp > 0xFFFF)
                    ++ci;
            }

            result.append(shaped);
            continue; // skip HarfBuzz path below
        }

        // Existing HarfBuzz path continues here...
        if (!face->hbFont)
            continue;

        // Configure HarfBuzz font size
        FT_F26Dot6 ftSize = static_cast<FT_F26Dot6>(style.fontSize * 64);
        hb_font_set_scale(face->hbFont, static_cast<int>(style.fontSize * 64),
                          static_cast<int>(style.fontSize * 64));
        FT_Set_Char_Size(face->ftFace, ftSize, 0, 72, 0);
        hb_ft_font_changed(face->hbFont);

        // Set up HarfBuzz buffer
        hb_buffer_t *buf = hb_buffer_create();
        hb_buffer_add_utf16(buf, text.utf16(), text.length(), run.start, run.length);
        hb_buffer_set_direction(buf, run.dir ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_set_script(buf, hb_icu_script_to_script(
            static_cast<UScriptCode>(run.script)));
        hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);

        // Font features
        QList<hb_feature_t> features;
        for (const QString &feat : style.fontFeatures) {
            hb_feature_t hbFeat;
            QByteArray featBytes = feat.toUtf8();
            if (hb_feature_from_string(featBytes.constData(), featBytes.size(), &hbFeat)) {
                hbFeat.start = 0;
                hbFeat.end = static_cast<unsigned int>(-1);
                features.append(hbFeat);
            }
        }

        // Shape
        const char *shapers[] = {"ot", "fallback", nullptr};
        hb_shape_full(face->hbFont, buf,
                      features.isEmpty() ? nullptr : features.data(),
                      features.size(), shapers);

        // Extract results
        unsigned int count = hb_buffer_get_length(buf);
        hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buf, nullptr);
        hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buf, nullptr);

        ShapedRun shaped;
        shaped.font = face;
        shaped.fontSize = style.fontSize;
        shaped.textStart = run.start;
        shaped.textLength = run.length;
        shaped.rtl = (run.dir != 0);
        shaped.glyphs.reserve(static_cast<int>(count));

        for (unsigned int i = 0; i < count; ++i) {
            ShapedGlyph g;
            g.glyphId = infos[i].codepoint;
            // HarfBuzz positions are in 26.6 fixed-point (when using hb-ft)
            // divided by 64 to get points
            g.xAdvance = positions[i].x_advance / 64.0;
            g.yAdvance = positions[i].y_advance / 64.0;
            g.xOffset = positions[i].x_offset / 64.0;
            g.yOffset = positions[i].y_offset / 64.0;
            g.cluster = static_cast<int>(infos[i].cluster);

            // Mark glyph as used for subsetting
            m_fontManager->markGlyphUsed(face, g.glyphId);
            shaped.glyphs.append(g);
        }

        result.append(shaped);
        hb_buffer_destroy(buf);
    }

    return result;
}
