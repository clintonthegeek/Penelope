// SPDX-License-Identifier: GPL-2.0-or-later
// HSV ring+triangle color selector, adapted from Krita's Advanced Color Selector.
// Original: SPDX-FileCopyrightText: 2010 Adam Celarek <kdedev at xibo dot at>

#ifndef PRETTYREADER_COLORSELECTORWIDGET_H
#define PRETTYREADER_COLORSELECTORWIDGET_H

#include <QImage>
#include <QWidget>

/**
 * Standalone HSV ring+triangle color selector widget.
 *
 * The outer ring selects hue; the inscribed triangle selects
 * saturation (horizontal) and value (vertical).
 */
class ColorSelectorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ColorSelectorWidget(QWidget *parent = nullptr);

    QColor color() const;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int w) const override;

public Q_SLOTS:
    /// Set the displayed color without emitting colorChanged.
    void setColor(const QColor &color);

Q_SIGNALS:
    /// Emitted on mouse release — triggers persistence.
    void colorChanged(const QColor &color);
    /// Emitted during drag (press/move) — display-only, no persistence.
    void colorPreview(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // Geometry
    int outerRadius() const;
    int innerRadius() const;
    int triangleHeight() const;
    int triangleWidth() const;
    QPoint widgetToTriangleCoords(const QPoint &p) const;
    QPoint triangleToWidgetCoords(const QPoint &p) const;

    // Hit testing
    bool ringContains(int x, int y) const;
    bool triangleContains(int x, int y) const;

    // Cache rebuilding
    void rebuildRingCache();
    void rebuildTriangleCache();

    // Painting helpers
    void paintRing(QPainter *p);
    void paintTriangle(QPainter *p);

    // Color picking
    void selectRingColor(int x, int y);
    void selectTriangleColor(int x, int y);
    void updateBlipPosition();

    // Interaction state
    enum GrabTarget { None, Ring, Triangle };
    GrabTarget m_grabbing = None;

    // Current color in HSV
    qreal m_hue = 0.0;
    qreal m_saturation = 1.0;
    qreal m_value = 1.0;

    // Ring pixel cache
    QImage m_ringCache;
    QList<QRgb> m_hueColors;
    int m_ringCachedSize = 0;

    // Triangle pixel cache
    QImage m_triangleCache;
    int m_triCachedW = 0;
    int m_triCachedH = 0;
    bool m_triangleDirty = true;

    // Triangle blip position (normalized 0..1 in widget coords, <0 = hidden)
    QPointF m_triangleBlip{-1, -1};

    static constexpr qreal s_innerRingFraction = 0.82;
};

#endif // PRETTYREADER_COLORSELECTORWIDGET_H
