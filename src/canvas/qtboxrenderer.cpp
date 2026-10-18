/*
 * qtboxrenderer.cpp — QPainter backend for BoxTreeRenderer
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qtboxrenderer.h"
#include "fontmanager.h"

#include <QGlyphRun>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRawFont>
#include <QTransform>

QtBoxRenderer::QtBoxRenderer(FontManager *fontManager)
    : BoxTreeRenderer(fontManager)
{
}

void QtBoxRenderer::setPainter(QPainter *painter)
{
    m_painter = painter;
}

// --- QRawFont cache ---

const QRawFont &QtBoxRenderer::rawFontFor(FontFace *face, qreal sizePoints)
{
    int sizeKey = qRound(sizePoints * 100);
    auto key = qMakePair(face, sizeKey);
    auto it = m_rawFontCache.find(key);
    if (it != m_rawFontCache.end())
        return it.value();

    QByteArray data = m_fontManager->rawFontData(face);
    auto inserted = m_rawFontCache.insert(key, QRawFont(data, sizePoints));
    return inserted.value();
}

// --- Drawing primitives ---

void QtBoxRenderer::drawRect(const QRectF &rect, const QColor &fill,
                              const QColor &stroke, qreal strokeWidth)
{
    pushState();
    if (fill.isValid()) {
        m_painter->setPen(Qt::NoPen);
        m_painter->setBrush(fill);
        m_painter->drawRect(rect);
    }
    if (stroke.isValid()) {
        m_painter->setPen(QPen(stroke, strokeWidth));
        m_painter->setBrush(Qt::NoBrush);
        m_painter->drawRect(rect);
    }
    popState();
}

void QtBoxRenderer::drawRoundedRect(const QRectF &rect, qreal xRadius, qreal yRadius,
                                     const QColor &fill, const QColor &stroke,
                                     qreal strokeWidth)
{
    pushState();
    if (fill.isValid()) {
        m_painter->setPen(Qt::NoPen);
        m_painter->setBrush(fill);
        m_painter->drawRoundedRect(rect, xRadius, yRadius);
    }
    if (stroke.isValid()) {
        m_painter->setPen(QPen(stroke, strokeWidth));
        m_painter->setBrush(Qt::NoBrush);
        m_painter->drawRoundedRect(rect, xRadius, yRadius);
    }
    popState();
}

void QtBoxRenderer::drawLine(const QPointF &p1, const QPointF &p2,
                              const QColor &color, qreal width)
{
    m_painter->save();
    m_painter->setPen(QPen(color, width));
    m_painter->drawLine(p1, p2);
    m_painter->restore();
}

void QtBoxRenderer::drawPolyline(const QPolygonF &poly, const QColor &color,
                                  qreal width, Qt::PenCapStyle cap,
                                  Qt::PenJoinStyle join)
{
    m_painter->save();
    m_painter->setPen(QPen(color, width, Qt::SolidLine, cap, join));
    m_painter->drawPolyline(poly);
    m_painter->restore();
}

void QtBoxRenderer::drawCheckmark(const QPolygonF &poly, const QColor &color,
                                   qreal width)
{
    m_painter->save();
    m_painter->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    m_painter->setBrush(Qt::NoBrush);
    m_painter->drawPolyline(poly);
    m_painter->restore();
}

// Qt's raster glyph cache silently drops glyphs whose rasterized pixel size
// reaches 64px. Above this threshold drawGlyphs fills outlines instead of
// using drawGlyphRun, so large text survives zoom instead of vanishing. The
// margin keeps small raw fonts, which fail while upscaling, on the safe side.
namespace {
constexpr qreal kMaxGlyphRunPixelSize = 40.0;
}

qreal QtBoxRenderer::painterScale() const
{
    if (!m_painter)
        return 1.0;
    const QTransform t = m_painter->combinedTransform();
    return qMax(qAbs(t.m11()), qAbs(t.m22()));
}

void QtBoxRenderer::drawGlyphsAsPaths(const QRawFont &rf, const GlyphRenderInfo &info,
                                      const QColor &foreground,
                                      qreal x, qreal baselineY)
{
    QPainterPath path;
    for (int i = 0; i < info.glyphIds.size(); ++i) {
        QPainterPath glyph = rf.pathForGlyph(info.glyphIds.at(i));
        if (glyph.isEmpty())
            continue;
        const QPointF offset = i < info.positions.size() ? info.positions.at(i) : QPointF();
        path.addPath(glyph.translated(offset));
    }

    m_painter->save();
    m_painter->setPen(Qt::NoPen);
    m_painter->setBrush(foreground);
    m_painter->translate(x, baselineY);
    m_painter->drawPath(path);
    m_painter->restore();
}

void QtBoxRenderer::drawGlyphs(FontFace *face, qreal fontSize,
                                const GlyphRenderInfo &info,
                                const QColor &foreground,
                                qreal x, qreal baselineY)
{
    const QRawFont &rf = rawFontFor(face, fontSize);
    if (!rf.isValid())
        return;

    if (fontSize * painterScale() > kMaxGlyphRunPixelSize) {
        drawGlyphsAsPaths(rf, info, foreground, x, baselineY);
        return;
    }

    QGlyphRun glyphRun;
    glyphRun.setRawFont(rf);
    glyphRun.setGlyphIndexes(info.glyphIds);
    glyphRun.setPositions(info.positions);

    m_painter->save();
    m_painter->setPen(QPen(foreground));
    // Glyph runs take their color from the pen. A solid brush left behind by a
    // page shadow or a viewport fill must not replace it.
    m_painter->setBrush(Qt::NoBrush);
    m_painter->drawGlyphRun(QPointF(x, baselineY), glyphRun);
    m_painter->restore();
}

void QtBoxRenderer::drawHersheyStrokes(const QVector<QVector<QPointF>> &strokes,
                                        const QTransform &transform,
                                        const QColor &foreground,
                                        qreal strokeWidth)
{
    m_painter->save();
    m_painter->setPen(QPen(foreground, strokeWidth,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    m_painter->setBrush(Qt::NoBrush);
    m_painter->setTransform(transform, true);

    for (const auto &poly : strokes) {
        m_painter->drawPolyline(QPolygonF(poly));
    }

    m_painter->restore();
}

void QtBoxRenderer::drawImage(const QRectF &destRect, const QImage &image)
{
    m_painter->drawImage(destRect, image);
}

void QtBoxRenderer::pushState()
{
    m_painter->save();
}

void QtBoxRenderer::popState()
{
    m_painter->restore();
}

void QtBoxRenderer::collectLink(const QRectF &rect, const QString &href)
{
    m_linkHitRects.append({rect, href});
}
