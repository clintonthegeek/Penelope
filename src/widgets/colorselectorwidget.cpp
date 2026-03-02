// SPDX-License-Identifier: GPL-2.0-or-later
// HSV ring+triangle color selector, adapted from Krita's Advanced Color Selector.
// Original: SPDX-FileCopyrightText: 2010 Adam Celarek <kdedev at xibo dot at>

#include "colorselectorwidget.h"

#include <QMouseEvent>
#include <QPainter>

#include <cmath>

// ---------------------------------------------------------------------------
// Fast inline HSV → RGB (avoids QColor allocation per pixel)
// ---------------------------------------------------------------------------

static inline QRgb hsvToRgb(qreal h, qreal s, qreal v)
{
    if (s <= 0.0) {
        int g = int(v * 255);
        return qRgb(g, g, g);
    }

    qreal hh = h * 6.0;
    if (hh >= 6.0)
        hh = 0.0;
    int i = int(hh);
    qreal f = hh - i;
    qreal p = v * (1.0 - s);
    qreal q = v * (1.0 - s * f);
    qreal t = v * (1.0 - s * (1.0 - f));

    qreal r, g, b;
    switch (i) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }

    return qRgb(int(r * 255), int(g * 255), int(b * 255));
}

// ===========================================================================
// Construction
// ===========================================================================

ColorSelectorWidget::ColorSelectorWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(80, 80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QColor ColorSelectorWidget::color() const
{
    return QColor::fromHsvF(float(m_hue), float(m_saturation), float(m_value));
}

QSize ColorSelectorWidget::minimumSizeHint() const
{
    return {120, 120};
}

void ColorSelectorWidget::setColor(const QColor &color)
{
    float h, s, v;
    color.getHsvF(&h, &s, &v);

    // Keep current hue for achromatic colours (h == -1 when s == 0)
    if (h >= 0 && !qFuzzyCompare(float(m_hue), h)) {
        m_hue = h;
        m_triangleDirty = true;
    }

    m_saturation = s;
    m_value = v;

    updateBlipPosition();
    update();
}

// ===========================================================================
// Events
// ===========================================================================

void ColorSelectorWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().window());

    paintRing(&p);
    paintTriangle(&p);
}

void ColorSelectorWidget::resizeEvent(QResizeEvent *)
{
    m_ringCachedSize = 0;   // force ring rebuild
    m_triangleDirty = true; // force triangle rebuild
    updateBlipPosition();
}

void ColorSelectorWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    const int x = int(event->position().x());
    const int y = int(event->position().y());

    if (ringContains(x, y)) {
        m_grabbing = Ring;
        selectRingColor(x, y);
    } else if (triangleContains(x, y)) {
        m_grabbing = Triangle;
        selectTriangleColor(x, y);
    }

    event->accept();
}

void ColorSelectorWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_grabbing == None)
        return;

    const int x = int(event->position().x());
    const int y = int(event->position().y());

    if (m_grabbing == Ring)
        selectRingColor(x, y);
    else if (m_grabbing == Triangle)
        selectTriangleColor(x, y);

    event->accept();
}

void ColorSelectorWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_grabbing != None) {
        m_grabbing = None;
        Q_EMIT colorChanged(color());
    }
    event->accept();
}

// ===========================================================================
// Geometry helpers
// ===========================================================================

int ColorSelectorWidget::outerRadius() const
{
    return qMin(width(), height()) / 2 - 1;
}

int ColorSelectorWidget::innerRadius() const
{
    return int(outerRadius() * s_innerRingFraction);
}

int ColorSelectorWidget::triangleHeight() const
{
    return int(innerRadius() * 2 * 3.0 / 4.0);
}

int ColorSelectorWidget::triangleWidth() const
{
    return int(triangleHeight() * 2.0 / std::sqrt(3.0));
}

QPoint ColorSelectorWidget::widgetToTriangleCoords(const QPoint &p) const
{
    const QPoint topLeft(width() / 2 - triangleWidth() / 2,
                         height() / 2 - int(triangleHeight() * (2.0 / 3.0)));
    return p - topLeft;
}

QPoint ColorSelectorWidget::triangleToWidgetCoords(const QPoint &p) const
{
    const QPoint topLeft(width() / 2 - triangleWidth() / 2,
                         height() / 2 - int(triangleHeight() * (2.0 / 3.0)));
    return topLeft + p;
}

// ===========================================================================
// Hit testing
// ===========================================================================

bool ColorSelectorWidget::ringContains(int x, int y) const
{
    const int dx = x - width() / 2;
    const int dy = y - height() / 2;
    const int distSq = dx * dx + dy * dy;
    const int outer = outerRadius();
    const int inner = innerRadius();
    return distSq <= outer * outer && distSq >= inner * inner;
}

bool ColorSelectorWidget::triangleContains(int x, int y) const
{
    const QPoint tc = widgetToTriangleCoords(QPoint(x, y));
    const int th = triangleHeight();
    const int tw = triangleWidth();

    if (tc.y() < 0 || tc.y() > th || tc.x() < 0 || tc.x() > tw)
        return false;

    const qreal lineLen = tc.y() * (2.0 / std::sqrt(3.0));
    const qreal lineStart = tw / 2.0 - lineLen / 2.0;
    const qreal lineEnd = lineStart + lineLen;

    return tc.x() >= lineStart && tc.x() <= lineEnd;
}

// ===========================================================================
// Ring cache
// ===========================================================================

void ColorSelectorWidget::rebuildRingCache()
{
    const int size = qMin(width(), height());
    const qreal dpr = devicePixelRatioF();

    // 360-entry hue lookup table
    m_hueColors.clear();
    m_hueColors.reserve(360);
    for (int i = 0; i < 360; ++i)
        m_hueColors.append(hsvToRgb(qreal(i) / 360.0, 1.0, 1.0));

    // Pixel cache at native resolution
    const int cacheSize = int(size * dpr);
    m_ringCache = QImage(cacheSize, cacheSize, QImage::Format_ARGB32_Premultiplied);
    m_ringCache.setDevicePixelRatio(dpr);
    m_ringCache.fill(Qt::transparent);

    const int cx = cacheSize / 2;
    const int cy = cacheSize / 2;
    const int outerR = int(outerRadius() * dpr);
    const int innerR = int(innerRadius() * dpr);

    for (int py = 0; py < cacheSize; ++py) {
        auto *line = reinterpret_cast<QRgb *>(m_ringCache.scanLine(py));
        for (int px = 0; px < cacheSize; ++px) {
            const int dx = px - cx;
            const int dy = py - cy;
            const qreal dist = std::sqrt(qreal(dx * dx + dy * dy));

            if (dist >= innerR - 1 && dist <= outerR + 1) {
                qreal angle = std::atan2(qreal(dy), qreal(dx)) + M_PI;
                angle = angle / (2.0 * M_PI) * 359.0;
                const int hueIdx = qBound(0, int(angle), 359);

                if (dist <= outerR && dist >= innerR) {
                    line[px] = m_hueColors.at(hueIdx);
                } else {
                    // Anti-aliased edge
                    qreal coef = (dist > outerR)
                                     ? 1.0 - (dist - outerR)
                                     : dist - innerR + 1.0;
                    coef = qBound(0.0, coef, 1.0);
                    const int r = qRed(m_hueColors.at(hueIdx));
                    const int g = qGreen(m_hueColors.at(hueIdx));
                    const int b = qBlue(m_hueColors.at(hueIdx));
                    // premultiplied alpha
                    line[px] = qRgba(int(r * coef), int(g * coef),
                                     int(b * coef), int(255 * coef));
                }
            }
        }
    }

    m_ringCachedSize = size;
}

// ===========================================================================
// Triangle cache
// ===========================================================================

void ColorSelectorWidget::rebuildTriangleCache()
{
    const int tw = triangleWidth();
    const int th = triangleHeight();
    if (tw <= 0 || th <= 0)
        return;

    const qreal dpr = devicePixelRatioF();
    const int imgW = int(tw * dpr) + 1;
    const int imgH = int(th * dpr) + 1;

    m_triangleCache = QImage(imgW, imgH, QImage::Format_ARGB32_Premultiplied);
    m_triangleCache.setDevicePixelRatio(dpr);
    m_triangleCache.fill(Qt::transparent);

    for (int py = 0; py < imgH; ++py) {
        auto *line = reinterpret_cast<QRgb *>(m_triangleCache.scanLine(py));
        const qreal y = py / dpr;
        if (y > th)
            continue;

        const qreal value = y / qreal(th);
        const qreal lineLen = y * (2.0 / std::sqrt(3.0));
        const qreal lineStart = tw / 2.0 - lineLen / 2.0;

        for (int px = 0; px < imgW; ++px) {
            const qreal x = px / dpr;
            const qreal relX = x - lineStart;

            if (relX >= 0 && relX <= lineLen && lineLen > 0.5) {
                const qreal saturation = relX / lineLen;
                line[px] = hsvToRgb(m_hue, saturation, value);
            }
        }
    }

    // Anti-alias the two sloped edges using CompositionMode_Clear
    QPainter gc(&m_triangleCache);
    gc.setRenderHint(QPainter::Antialiasing);
    gc.setPen(QPen(QColor(0, 0, 0, 128), 2.5));
    gc.setCompositionMode(QPainter::CompositionMode_Clear);
    gc.drawLine(QPointF(0, th), QPointF(tw / 2.0, 0));
    gc.drawLine(QPointF(tw / 2.0 + 1.0, 0), QPointF(tw + 1, th));

    m_triCachedW = tw;
    m_triCachedH = th;
    m_triangleDirty = false;
}

// ===========================================================================
// Painting
// ===========================================================================

void ColorSelectorWidget::paintRing(QPainter *p)
{
    const int size = qMin(width(), height());
    if (m_ringCachedSize != size)
        rebuildRingCache();

    const qreal dpr = devicePixelRatioF();
    const int startX = width() / 2 - int(m_ringCache.width() / (2 * dpr));
    const int startY = height() / 2 - int(m_ringCache.height() / (2 * dpr));
    p->drawImage(startX, startY, m_ringCache);

    // Hue blip: two parallel radial lines (black + white) for contrast
    const qreal angle = m_hue * 2.0 * M_PI + M_PI;
    const int inner = innerRadius();
    const int outer = outerRadius();
    const int hw = width() / 2;
    const int hh = height() / 2;

    auto lineAt = [&](qreal a) {
        return QLineF(inner * std::cos(a) + hw, inner * std::sin(a) + hh,
                       outer * std::cos(a) + hw, outer * std::sin(a) + hh);
    };

    p->setPen(QPen(Qt::black, 1.5));
    p->drawLine(lineAt(angle));
    p->setPen(QPen(Qt::white, 1.5));
    p->drawLine(lineAt(angle + M_PI / 180.0));
}

void ColorSelectorWidget::paintTriangle(QPainter *p)
{
    const int tw = triangleWidth();
    const int th = triangleHeight();
    if (tw <= 0 || th <= 0)
        return;

    if (m_triangleDirty || m_triCachedW != tw || m_triCachedH != th)
        rebuildTriangleCache();

    const int imgX = width() / 2 - tw / 2;
    const int imgY = height() / 2 - int(th * (2.0 / 3.0));
    p->drawImage(imgX, imgY, m_triangleCache);

    // SV blip: concentric circles (black outer, white inner) for contrast
    if (m_triangleBlip.x() > -0.1) {
        const int bx = int(m_triangleBlip.x() * width());
        const int by = int(m_triangleBlip.y() * height());

        p->setPen(QPen(Qt::black, 1.5));
        p->drawEllipse(bx - 5, by - 5, 10, 10);
        p->setPen(QPen(Qt::white, 1.5));
        p->drawEllipse(bx - 4, by - 4, 8, 8);
    }
}

// ===========================================================================
// Color selection
// ===========================================================================

void ColorSelectorWidget::selectRingColor(int x, int y)
{
    const int dx = x - width() / 2;
    const int dy = y - height() / 2;
    qreal hue = (std::atan2(qreal(dy), qreal(dx)) + M_PI) / (2.0 * M_PI);

    m_hue = qBound(0.0, hue, 1.0 - 1e-10);
    m_triangleDirty = true;

    update();
}

void ColorSelectorWidget::selectTriangleColor(int x, int y)
{
    QPoint tc = widgetToTriangleCoords(QPoint(x, y));
    const int th = triangleHeight();
    const int tw = triangleWidth();

    // Clamp to triangle bounds
    tc.setY(qBound(0, tc.y(), th));

    const qreal lineLen = tc.y() * (2.0 / std::sqrt(3.0));
    const qreal lineStart = tw / 2.0 - lineLen / 2.0;
    const qreal lineEnd = lineStart + lineLen;
    tc.setX(qBound(int(lineStart), tc.x(), int(lineEnd)));

    m_value = (th > 0) ? qBound(0.0, qreal(tc.y()) / qreal(th), 1.0) : 0.0;
    m_saturation = (lineLen > 0.5) ? qBound(0.0, (tc.x() - lineStart) / lineLen, 1.0) : 0.0;

    // Update blip
    const QPoint wp = triangleToWidgetCoords(tc);
    m_triangleBlip.setX(wp.x() / qreal(width()));
    m_triangleBlip.setY(wp.y() / qreal(height()));

    update();
}

void ColorSelectorWidget::updateBlipPosition()
{
    const int th = triangleHeight();
    const int tw = triangleWidth();
    if (th <= 0 || tw <= 0 || width() <= 0 || height() <= 0)
        return;

    const qreal y = m_value * th;
    const qreal lineLen = y * (2.0 / std::sqrt(3.0));
    const qreal lineStart = tw / 2.0 - lineLen / 2.0;
    const qreal x = m_saturation * lineLen + lineStart;

    const QPoint wp = triangleToWidgetCoords(QPoint(int(x), int(y)));
    m_triangleBlip.setX(wp.x() / qreal(width()));
    m_triangleBlip.setY(wp.y() / qreal(height()));
}
