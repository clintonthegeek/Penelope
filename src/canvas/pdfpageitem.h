/*
 * pdfpageitem.h — Poppler-backed QGraphicsItem with async render
 *
 * Displays a single PDF page by requesting renders from RenderCache.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PDFPAGEITEM_H
#define PENELOPE_PDFPAGEITEM_H

#include <QColor>
#include <QGraphicsItem>
#include <QSizeF>

class RenderCache;

class PdfPageItem : public QGraphicsItem {
public:
    PdfPageItem(int pageNumber, QSizeF pageSize, RenderCache *cache,
                QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    void setPageNumber(int page);
    int pageNumber() const { return m_pageNumber; }
    QSizeF pageSize() const { return m_pageSize; }

    void setZoomFactor(qreal zoom);
    void setPageBackground(const QColor &color) { m_pageBackground = color; update(); }

    // B2: Selection highlight
    void setSelectionRects(const QList<QRectF> &rects);
    void clearSelection();

private:
    int m_pageNumber;
    QSizeF m_pageSize; // in points
    RenderCache *m_cache;
    qreal m_zoom = 1.0;
    QColor m_pageBackground = Qt::white;
    QList<QRectF> m_selectionRects;  // page-local coords
};

#endif // PENELOPE_PDFPAGEITEM_H
