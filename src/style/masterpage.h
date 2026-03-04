#ifndef PENELOPE_MASTERPAGE_H
#define PENELOPE_MASTERPAGE_H

#include <QMarginsF>
#include <QString>

// A master page defines per-page-type overrides for headers, footers, and margins.
// Page types: "first" (first page of document/chapter), "left" (even pages),
// "right" (odd pages). If a property is not set (empty string / negative margin),
// the base PageLayout values are used.

struct MasterPage
{
    QString name;  // "first", "left", "right"

    // Header overrides (-1 = use base layout value, 0/1 = explicit)
    int headerEnabled = -1;  // -1 = inherit, 0 = disabled, 1 = enabled
    int footerEnabled = -1;

    QString headerLeft;
    QString headerCenter;
    QString headerRight;
    QString footerLeft;
    QString footerCenter;
    QString footerRight;

    // Whether header/footer field strings are explicitly set
    bool hasHeaderLeft = false;
    bool hasHeaderCenter = false;
    bool hasHeaderRight = false;
    bool hasFooterLeft = false;
    bool hasFooterCenter = false;
    bool hasFooterRight = false;

    // Margin overrides (negative = inherit from base layout)
    qreal marginTop = -1;
    qreal marginBottom = -1;
    qreal marginLeft = -1;
    qreal marginRight = -1;

    bool isDefault() const
    {
        return headerEnabled < 0 && footerEnabled < 0
            && !hasHeaderLeft && !hasHeaderCenter && !hasHeaderRight
            && !hasFooterLeft && !hasFooterCenter && !hasFooterRight
            && marginTop < 0 && marginBottom < 0
            && marginLeft < 0 && marginRight < 0;
    }
};

#endif // PENELOPE_MASTERPAGE_H
