#ifndef PENELOPE_HITINDEX_H
#define PENELOPE_HITINDEX_H

#include "layoutengine.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

struct HitIndex {
    QVector<QRectF> lineBounds;
    QVector<int> sourceStart;
    QVector<int> sourceEnd;
    QVector<QString> lineText;
    QVector<QRectF> linkBounds;
    QVector<QString> linkHrefs;
    QVector<QString> linkTitles;
    // Images that declare a title, so hovering one can say what it is.
    QVector<QRectF> imageBounds;
    QVector<QString> imageTitles;
};

HitIndex buildHitIndex(const Layout::Page &page);
int hitLine(const HitIndex &index, QPointF pagePos);

#endif
