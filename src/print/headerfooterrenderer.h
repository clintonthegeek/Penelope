#ifndef PENELOPE_HEADERFOOTERRENDERER_H
#define PENELOPE_HEADERFOOTERRENDERER_H

#include <QRectF>
#include <QString>

class QPainter;
struct PageLayout;

struct PageMetadata {
    int pageNumber = 0;   // 0-based
    int totalPages = 1;
    QString fileName;
    QString title;
};

namespace HeaderFooterRenderer {

void drawHeader(QPainter *painter, const QRectF &rect,
                const PageLayout &layout, const PageMetadata &meta,
                qreal separatorWidth);

void drawFooter(QPainter *painter, const QRectF &rect,
                const PageLayout &layout, const PageMetadata &meta,
                qreal separatorWidth);

QString resolveField(const QString &text, const PageMetadata &meta);

} // namespace HeaderFooterRenderer

#endif // PENELOPE_HEADERFOOTERRENDERER_H
