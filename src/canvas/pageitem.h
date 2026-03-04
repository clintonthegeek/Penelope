#ifndef PENELOPE_PAGEITEM_H
#define PENELOPE_PAGEITEM_H

#include <QGraphicsItem>
#include <QMarginsF>
#include <QString>
#include <QTextDocument>

#include "pagelayout.h"

class PageItem : public QGraphicsItem
{
public:
    PageItem(int pageNumber, QSizeF pageSize, QTextDocument *document,
             const QMarginsF &margins = QMarginsF(),
             QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    int pageNumber() const { return m_pageNumber; }
    void setPageNumber(int page) { m_pageNumber = page; }

    void setPageLayout(const PageLayout &layout);
    void setDocumentInfo(int totalPages, const QString &fileName, const QString &title);

private:
    int m_pageNumber;
    QSizeF m_pageSize;
    QTextDocument *m_document;
    QMarginsF m_margins;
    PageLayout m_pageLayout;
    int m_totalPages = 1;
    QString m_fileName;
    QString m_title;
};

#endif // PENELOPE_PAGEITEM_H
