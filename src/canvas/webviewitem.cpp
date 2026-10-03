/*
 * webviewitem.cpp — QGraphicsItem for continuous web view rendering
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "webviewitem.h"

#include <QPainter>
#include <QApplication>
#include <QStyleOptionGraphicsItem>

WebViewItem::WebViewItem(FontManager *fontManager)
    : m_renderer(fontManager)
{
}

void WebViewItem::setLayoutResult(Layout::ContinuousLayoutResult &&result)
{
    prepareGeometryChange();
    m_result = std::move(result);
    m_renderer.clearLinkHitRects();
    update();
}

QRectF WebViewItem::boundingRect() const
{
    return QRectF(0, 0, m_result.contentWidth, m_result.totalHeight);
}

// Binary search for the first element whose bottom edge is >= top
int WebViewItem::firstVisibleElement(qreal top) const
{
    int lo = 0;
    int hi = m_result.elements.size();
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        qreal elemBottom = 0;
        std::visit([&](const auto &e) {
            elemBottom = e.y + e.height;
        }, m_result.elements[mid]);

        if (elemBottom < top)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

void WebViewItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                         QWidget * /*widget*/)
{
    QRectF exposed = option->exposedRect;

    // Page background
    painter->fillRect(exposed, m_pageBackground);

    m_renderer.clearLinkHitRects();
    m_renderer.setPainter(painter);

    int startIdx = firstVisibleElement(exposed.top());

    for (int i = startIdx; i < m_result.elements.size(); ++i) {
        const auto &element = m_result.elements[i];

        qreal elemY = 0;
        std::visit([&](const auto &e) {
            elemY = e.y;
        }, element);

        // Stop if we've passed the visible area
        if (elemY > exposed.bottom())
            break;

        m_renderer.renderElement(element);
    }
    QColor color = QApplication::palette().color(QPalette::Highlight);
    color.setAlpha(80);
    for (const auto &rect : m_selectionRects) painter->fillRect(rect, color);
}

QString WebViewItem::linkAt(const QPointF &pos) const
{
    for (const auto &link : m_renderer.linkHitRects()) {
        if (link.rect.contains(pos))
            return link.href;
    }
    return {};
}
