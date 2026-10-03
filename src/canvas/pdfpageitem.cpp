/*
 * pdfpageitem.cpp — Poppler-backed QGraphicsItem
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pdfpageitem.h"
#include "rendercache.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionGraphicsItem>

PdfPageItem::PdfPageItem(int pageNumber, QSizeF pageSize, RenderCache *cache,
                         QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_pageNumber(pageNumber)
    , m_pageSize(pageSize)
    , m_cache(cache)
{
}

QRectF PdfPageItem::boundingRect() const
{
    // Page shadow offset
    constexpr qreal shadow = 4.0;
    return QRectF(-1, -1, m_pageSize.width() + shadow + 2, m_pageSize.height() + shadow + 2);
}

void PdfPageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                        QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    QRectF pageRect(0, 0, m_pageSize.width(), m_pageSize.height());

    // Drop shadow
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 50));
    painter->drawRect(pageRect.translated(4, 4));

    // Page background
    painter->setBrush(m_pageBackground);
    painter->setPen(QPen(QColor(0xcc, 0xcc, 0xcc), 0.5));
    painter->drawRect(pageRect);

    // Render from cache
    int renderWidth = qRound(m_pageSize.width() * m_zoom);
    int renderHeight = qRound(m_pageSize.height() * m_zoom);

    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    QImage img = m_cache->cachedPixmap(m_pageNumber, renderWidth, renderHeight, dpr);
    if (!img.isNull()) {
        painter->drawImage(pageRect, img);
    } else {
        // Request render
        RenderCache::Request req;
        req.pageNumber = m_pageNumber;
        req.width = renderWidth;
        req.height = renderHeight;
        req.dpr = dpr;
        m_cache->requestPixmap(req);

        // Placeholder
        painter->setPen(QColor(0xaa, 0xaa, 0xaa));
        painter->drawText(pageRect, Qt::AlignCenter,
                          QStringLiteral("Loading page %1...").arg(m_pageNumber + 1));
    }

    // B2: Draw selection highlights
    if (!m_selectionRects.isEmpty()) {
        painter->setPen(Qt::NoPen);
        QColor selColor = QApplication::palette().color(QPalette::Highlight);
        selColor.setAlpha(80);
        painter->setBrush(selColor);
        for (const QRectF &r : m_selectionRects)
            painter->drawRect(r);
    }
}

void PdfPageItem::setPageNumber(int page)
{
    m_pageNumber = page;
    update();
}

void PdfPageItem::setZoomFactor(qreal zoom)
{
    m_zoom = zoom;
    update();
}

void PdfPageItem::setSelectionRects(const QList<QRectF> &rects)
{
    m_selectionRects = rects;
    update();
}

void PdfPageItem::clearSelection()
{
    if (!m_selectionRects.isEmpty()) {
        m_selectionRects.clear();
        update();
    }
}
