#ifndef PENELOPE_BOXPAGEITEM_H
#define PENELOPE_BOXPAGEITEM_H

#include "layoutengine.h"
#include "qtboxrenderer.h"

#include <QGraphicsItem>
#include <QSizeF>
#include <memory>

class FontManager;

class BoxPageItem : public QGraphicsItem {
public:
    BoxPageItem(std::shared_ptr<const Layout::Page> page, QSizeF pageSize,
                std::shared_ptr<FontManager> fonts, QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    std::shared_ptr<const Layout::Page> page() const { return m_page; }

private:
    std::shared_ptr<const Layout::Page> m_page;
    QSizeF m_pageSize;
    std::shared_ptr<FontManager> m_fonts;
    QtBoxRenderer m_renderer;
};

#endif
