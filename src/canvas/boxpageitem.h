#ifndef PENELOPE_BOXPAGEITEM_H
#define PENELOPE_BOXPAGEITEM_H

#include "hitindex.h"
#include "layoutengine.h"
#include "pagelayout.h"
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
    QSizeF pageSize() const { return m_pageSize; }
    int pageNumber() const { return m_page ? m_page->pageNumber : 0; }
    const HitIndex &hitIndex() const { return m_hitIndex; }
    QPointF contentOrigin() const;

    void setPageChrome(const PageLayout &layout, int totalPages,
                       const QString &fileName, const QString &title);

    QString linkAt(QPointF itemPos) const;
    QString linkTitleAt(QPointF itemPos) const;
    QRectF lineRect(int index) const;
    QString textInRect(const QRectF &itemRect) const;

private:
    QPointF toContent(QPointF itemPos) const;

    std::shared_ptr<const Layout::Page> m_page;
    QSizeF m_pageSize;
    std::shared_ptr<FontManager> m_fonts;
    QtBoxRenderer m_renderer;
    HitIndex m_hitIndex;
    PageLayout m_layout;
    int m_totalPages = 1;
    QString m_fileName;
    QString m_title;
};

#endif
