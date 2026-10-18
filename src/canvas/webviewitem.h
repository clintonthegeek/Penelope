/*
 * webviewitem.h — QGraphicsItem for continuous web view rendering
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_WEBVIEWITEM_H
#define PENELOPE_WEBVIEWITEM_H

#include "layoutengine.h"
#include "qtboxrenderer.h"

#include <QColor>
#include <memory>
#include <QGraphicsItem>

class FontManager;

class WebViewItem : public QGraphicsItem
{
public:
    explicit WebViewItem(FontManager *fontManager, std::shared_ptr<FontManager> owner = {});

    void setLayoutResult(Layout::ContinuousLayoutResult &&result);
    void appendElements(QList<Layout::PageElement> elements, qreal newHeight);
    const Layout::ContinuousLayoutResult &layoutResult() const { return m_result; }
    Layout::ContinuousLayoutResult &layoutResult() { return m_result; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    void setPageBackground(const QColor &color) { m_pageBackground = color; update(); }

    const QList<LinkHitRect> &linkHitRects() const { return m_paintedLinks; }
    void setSelectionRects(const QList<QRectF> &rects) { m_selectionRects = rects; update(); }
    QString linkAt(const QPointF &pos) const;
    QString linkTitleAt(const QPointF &pos) const;

private:
    int firstVisibleElement(qreal top) const;

    Layout::ContinuousLayoutResult m_result;
    std::shared_ptr<FontManager> m_fontOwner;
    QtBoxRenderer m_renderer;
    // A repaint may expose just a scrolling stripe. Retain hit rectangles
    // for previously painted elements until the immutable layout changes.
    QHash<int, QList<LinkHitRect>> m_linksByElement;
    QList<LinkHitRect> m_paintedLinks;
    QList<QRectF> m_selectionRects;
    QColor m_pageBackground = Qt::white;
};

#endif // PENELOPE_WEBVIEWITEM_H
