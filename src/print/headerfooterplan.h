#ifndef PENELOPE_HEADERFOOTERPLAN_H
#define PENELOPE_HEADERFOOTERPLAN_H

#include "pagelayout.h"

#include <QRectF>
#include <QString>

struct HeaderFooterField {
    QString text;
    QRectF rect;
};

struct HeaderFooterPlan {
    HeaderFooterField headerLeft;
    HeaderFooterField headerCenter;
    HeaderFooterField headerRight;
    HeaderFooterField footerLeft;
    HeaderFooterField footerCenter;
    HeaderFooterField footerRight;
    QRectF pageRect;
    qreal fontSize = 9.0;
    bool headerEnabled = false;
    bool footerEnabled = false;
};

HeaderFooterPlan makeHeaderFooterPlan(const PageLayout &pageLayout,
                                       int pageNumber,
                                       int totalPages,
                                       const QString &fileName,
                                       const QString &title);

bool headerFooterUsesPageToken(const PageLayout &pageLayout);

#endif
