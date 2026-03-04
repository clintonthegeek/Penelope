/*
 * qtboxrenderer.h — QPainter backend for BoxTreeRenderer
 *
 * Implements all virtual drawing primitives via QPainter, replacing
 * the monolithic WebViewRenderer with a clean subclass of the shared
 * box-tree traversal base class.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_QTBOXRENDERER_H
#define PENELOPE_QTBOXRENDERER_H

#include "boxtreerenderer.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QRawFont>
#include <QRectF>
#include <QString>

class FontManager;
class QPainter;
struct FontFace;

struct LinkHitRect {
    QRectF rect;
    QString href;
};

class QtBoxRenderer : public BoxTreeRenderer
{
public:
    explicit QtBoxRenderer(FontManager *fontManager);

    /// Set the QPainter to use for rendering.  Must be called before any
    /// render method; the caller retains ownership of the painter.
    void setPainter(QPainter *painter);

    /// Link hit-test rectangles collected during the last render pass.
    const QList<LinkHitRect> &linkHitRects() const { return m_linkHitRects; }

    /// Clear accumulated link hit rectangles (call before each render pass).
    void clearLinkHitRects() { m_linkHitRects.clear(); }

    // --- Drawing primitives (BoxTreeRenderer overrides) ---

    void drawRect(const QRectF &rect, const QColor &fill,
                  const QColor &stroke = QColor(),
                  qreal strokeWidth = 0) override;

    void drawRoundedRect(const QRectF &rect, qreal xRadius, qreal yRadius,
                         const QColor &fill, const QColor &stroke = QColor(),
                         qreal strokeWidth = 0) override;

    void drawLine(const QPointF &p1, const QPointF &p2,
                  const QColor &color, qreal width = 0.5) override;

    void drawPolyline(const QPolygonF &poly, const QColor &color,
                      qreal width, Qt::PenCapStyle cap = Qt::FlatCap,
                      Qt::PenJoinStyle join = Qt::MiterJoin) override;

    void drawCheckmark(const QPolygonF &poly, const QColor &color,
                       qreal width) override;

    void drawGlyphs(FontFace *face, qreal fontSize,
                    const GlyphRenderInfo &info,
                    const QColor &foreground,
                    qreal x, qreal baselineY) override;

    void drawHersheyStrokes(const QVector<QVector<QPointF>> &strokes,
                            const QTransform &transform,
                            const QColor &foreground,
                            qreal strokeWidth) override;

    void drawImage(const QRectF &destRect, const QImage &image) override;

    void pushState() override;
    void popState() override;

    void collectLink(const QRectF &rect, const QString &href) override;

private:
    const QRawFont &rawFontFor(FontFace *face, qreal sizePoints);

    QPainter *m_painter = nullptr;
    QHash<QPair<FontFace *, int>, QRawFont> m_rawFontCache; // key: (face, size*100)
    QList<LinkHitRect> m_linkHitRects;
};

#endif // PENELOPE_QTBOXRENDERER_H
