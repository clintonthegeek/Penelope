/*
 * boxtreerenderer.h — Base class for box tree rendering backends
 *
 * Declares virtual drawing primitives that each backend (QPainter, PDF)
 * implements.  Shared traversal logic lives in the base; backends only
 * override the primitives that map to their native API.
 *
 * All coordinates use the layout engine's top-down system.  Each backend
 * transforms to its native coordinate system inside the primitive.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_BOXTREERENDERER_H
#define PENELOPE_BOXTREERENDERER_H

#include "layoutengine.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QVector>

class FontManager;
struct FontFace;

/// Named constants for superscript/subscript positioning.
namespace RenderConstants {
    constexpr qreal kSuperscriptRise = 0.35;
    constexpr qreal kSubscriptDrop = 0.15;
}

/// Glyph IDs and positions relative to the drawing origin (x, baselineY).
struct GlyphRenderInfo {
    QVector<quint32> glyphIds;
    QVector<QPointF> positions; // relative to (x, baselineY)
};

/// Result of justification parameter computation.
struct JustifyParams {
    bool doJustify = false;
    qreal extraPerGap = 0;
    qreal extraPerChar = 0;
};

class BoxTreeRenderer
{
public:
    virtual ~BoxTreeRenderer();

    // --- Traversal (shared box tree walk calling virtual primitives) ---

    virtual void renderElement(const Layout::PageElement &element);
    virtual void renderBlockBox(const Layout::BlockBox &box);
    virtual void renderTableBox(const Layout::TableBox &box);
    virtual void renderFootnoteSectionBox(const Layout::FootnoteSectionBox &box);
    virtual void renderLineBox(const Layout::LineBox &line,
                               qreal originX, qreal originY, qreal availWidth);
    virtual void renderGlyphBox(const Layout::GlyphBox &gbox,
                                qreal x, qreal baselineY);
    virtual void renderHersheyGlyphBox(const Layout::GlyphBox &gbox,
                                       qreal x, qreal baselineY);
    virtual void renderGlyphDecorations(const Layout::GlyphBox &gbox,
                                        qreal x, qreal baselineY, qreal endX);
    virtual void renderCheckbox(const Layout::GlyphBox &gbox,
                                qreal x, qreal baselineY);
    virtual void renderImageBlock(const Layout::BlockBox &box);

    // --- Drawing primitives (pure virtual — each backend implements) ---

    /// Fill and/or stroke a rectangle.
    virtual void drawRect(const QRectF &rect, const QColor &fill,
                          const QColor &stroke = QColor(),
                          qreal strokeWidth = 0) = 0;

    /// Fill and/or stroke a rounded rectangle.
    virtual void drawRoundedRect(const QRectF &rect, qreal xRadius, qreal yRadius,
                                 const QColor &fill, const QColor &stroke = QColor(),
                                 qreal strokeWidth = 0) = 0;

    /// Draw a straight line between two points.
    virtual void drawLine(const QPointF &p1, const QPointF &p2,
                          const QColor &color, qreal width = 0.5) = 0;

    /// Draw a connected polyline (open path, not closed).
    virtual void drawPolyline(const QPolygonF &poly, const QColor &color,
                              qreal width, Qt::PenCapStyle cap = Qt::FlatCap,
                              Qt::PenJoinStyle join = Qt::MiterJoin) = 0;

    /// Draw a checkmark polyline (convenience — uses round caps/joins).
    virtual void drawCheckmark(const QPolygonF &poly, const QColor &color,
                               qreal width) = 0;

    /// Draw TTF/OTF glyphs at (x, baselineY) using the given font face/size.
    virtual void drawGlyphs(FontFace *face, qreal fontSize,
                            const GlyphRenderInfo &info,
                            const QColor &foreground,
                            qreal x, qreal baselineY) = 0;

    /// Draw Hershey vector font strokes with a combined transform.
    /// The strokes list contains polylines already in glyph-local coordinates.
    virtual void drawHersheyStrokes(const QVector<QVector<QPointF>> &strokes,
                                    const QTransform &transform,
                                    const QColor &foreground,
                                    qreal strokeWidth) = 0;

    /// Draw an image into a destination rectangle.
    virtual void drawImage(const QRectF &destRect, const QImage &image) = 0;

    /// Save the current graphics state (clip, transform, pen, brush, etc.).
    virtual void pushState() = 0;

    /// Restore the most recently saved graphics state.
    virtual void popState() = 0;

    /// Collect a link annotation for the current page/view.
    /// Coordinates are in layout-engine space; the backend converts as needed.
    virtual void collectLink(const QRectF &rect, const QString &href) = 0;

protected:
    explicit BoxTreeRenderer(FontManager *fontManager);

    /// Draw the inline background rectangle behind a glyph box (if any).
    void drawInlineBackground(const Layout::GlyphBox &gbox,
                              qreal x, qreal baselineY);

    FontManager *m_fontManager;

    // --- Shared justification helpers ---

    /// Compute justification parameters for a line.
    /// @param maxJustifyGap  maximum per-gap expansion before giving up
    ///                       (WebView uses 20 pt, PDF uses its m_maxJustifyGap).
    JustifyParams computeJustification(const Layout::LineBox &line,
                                       qreal availWidth,
                                       qreal maxJustifyGap = 20.0) const;

    /// Compute x-position of each glyph box in a justified/aligned line.
    /// Returns a list the same size as line.glyphs.
    QList<qreal> computeGlyphXPositions(const Layout::LineBox &line,
                                        qreal originX,
                                        qreal availWidth) const;
};

#endif // PENELOPE_BOXTREERENDERER_H
