#ifndef PENELOPE_HEADERFOOTERPLAN_H
#define PENELOPE_HEADERFOOTERPLAN_H

#include "pagelayout.h"

#include <QColor>
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
    QColor color = QColor(0x88, 0x88, 0x88);
    bool bold = false;
    bool italic = false;
    bool headerEnabled = false;
    bool footerEnabled = false;
    // Where the header and footer rules sit, and how far they span.
    QRectF headerRule;
    QRectF footerRule;
    bool hasHeaderRule = false;
    bool hasFooterRule = false;
};

HeaderFooterPlan makeHeaderFooterPlan(const PageLayout &pageLayout,
                                       int pageNumber,
                                       int totalPages,
                                       const QString &fileName,
                                       const QString &title);

bool headerFooterUsesPageToken(const PageLayout &pageLayout);

#endif
