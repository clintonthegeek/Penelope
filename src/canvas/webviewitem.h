/*
 * webviewitem.h — QGraphicsItem for continuous web view rendering
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_WEBVIEWITEM_H
#define PENELOPE_WEBVIEWITEM_H

#include "layoutengine.h"
#include "qtboxrenderer.h"

#include <QColor>
#include <QGraphicsItem>

class FontManager;

class WebViewItem : public QGraphicsItem
{
public:
    explicit WebViewItem(FontManager *fontManager);

    void setLayoutResult(Layout::ContinuousLayoutResult &&result);
    const Layout::ContinuousLayoutResult &layoutResult() const { return m_result; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    void setPageBackground(const QColor &color) { m_pageBackground = color; update(); }

    const QList<LinkHitRect> &linkHitRects() const { return m_renderer.linkHitRects(); }
    QString linkAt(const QPointF &pos) const;

private:
    int firstVisibleElement(qreal top) const;

    Layout::ContinuousLayoutResult m_result;
    QtBoxRenderer m_renderer;
    QColor m_pageBackground = Qt::white;
};

#endif // PENELOPE_WEBVIEWITEM_H
