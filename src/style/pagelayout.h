#ifndef PENELOPE_PAGELAYOUT_H
#define PENELOPE_PAGELAYOUT_H

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QSizeF>
#include <QString>

#include "masterpage.h"

// The page sizes the UI offers and the tokens they are saved as.
namespace PageSizes {
QList<QPageSize::PageSizeId> offered();
QString label(QPageSize::PageSizeId id);
QString token(QPageSize::PageSizeId id);
QPageSize::PageSizeId fromToken(const QString &token);
} // namespace PageSizes

struct PageLayout
{
    // JSON serialization
    static PageLayout fromJson(const QJsonObject &pageLayoutObj,
                               const QJsonObject &masterPagesObj);
    QJsonObject toPageLayoutJson() const;
    QJsonObject toMasterPagesJson() const;

    QPageSize::PageSizeId pageSizeId = QPageSize::A4;
    // The page's own size in mm, used when pageSizeId is Custom. Empty means
    // "no custom size has been given".
    QSizeF customSizeMm;
    QPageLayout::Orientation orientation = QPageLayout::Portrait;
    QMarginsF margins{25.0, 25.0, 25.0, 25.0}; // mm
    QColor pageBackground = Qt::white;

    // Header/footer configuration
    bool headerEnabled = false;
    bool footerEnabled = true;
    QString headerLeft;
    QString headerCenter;
    QString headerRight;
    QString footerLeft;
    QString footerCenter;
    QString footerRight{QStringLiteral("{page} / {pages}")};

    // Header and footer text: page furniture, so it travels with the template
    // rather than with the content styles.
    qreal chromeFontSize = 9.0;
    QColor chromeColor = QColor(0x88, 0x88, 0x88);
    bool chromeBold = false;
    bool chromeItalic = false;

    // Height constants (points)
    static constexpr qreal kHeaderHeight = 16.0;
    static constexpr qreal kFooterHeight = 14.0;
    static constexpr qreal kSeparatorGap = 6.0;

    qreal headerTotalHeight() const
    {
        return headerEnabled ? (kHeaderHeight + kSeparatorGap) : 0.0;
    }

    qreal footerTotalHeight() const
    {
        return footerEnabled ? (kFooterHeight + kSeparatorGap) : 0.0;
    }

    // Return the content area size in points (72 dpi)
    // Subtracts margins and header/footer heights from the full page size.
    QSizeF contentSizePoints() const
    {
        const QSizeF full = pageSizePoints();

        // Convert mm margins to points (1mm = 2.8346pt)
        constexpr qreal mmToPt = 72.0 / 25.4;
        qreal left   = margins.left()   * mmToPt;
        qreal right  = margins.right()  * mmToPt;
        qreal top    = margins.top()    * mmToPt;
        qreal bottom = margins.bottom() * mmToPt;

        return QSizeF(full.width() - left - right,
                      full.height() - top - bottom
                          - headerTotalHeight() - footerTotalHeight());
    }

    bool hasCustomSize() const
    {
        return pageSizeId == QPageSize::Custom
            && customSizeMm.isValid()
            && customSizeMm.width() > 0 && customSizeMm.height() > 0;
    }

    // Return the full page size in points
    QSizeF pageSizePoints() const
    {
        constexpr qreal mmToPt = 72.0 / 25.4;
        QSizeF full;
        if (hasCustomSize()) {
            full = QSizeF(customSizeMm.width() * mmToPt, customSizeMm.height() * mmToPt);
        } else {
            // An unknown id (or Custom without dimensions) falls back to A4
            // rather than to a zero-sized page.
            QPageSize ps(pageSizeId);
            full = ps.size(QPageSize::Point);
            if (full.width() <= 0 || full.height() <= 0)
                full = QPageSize(QPageSize::A4).size(QPageSize::Point);
        }
        if (orientation == QPageLayout::Landscape)
            full.transpose();
        return full;
    }

    // Return margins in points
    QMarginsF marginsPoints() const
    {
        constexpr qreal mmToPt = 72.0 / 25.4;
        return QMarginsF(margins.left()   * mmToPt,
                         margins.top()    * mmToPt,
                         margins.right()  * mmToPt,
                         margins.bottom() * mmToPt);
    }

    // Master page templates: "first", "left", "right"
    QHash<QString, MasterPage> masterPages;

    // Resolve the effective PageLayout for a given page, applying
    // master page overrides for the page type.
    // pageNumber is 0-based, isChapterStart indicates first page or chapter opener.
    PageLayout resolvedForPage(int pageNumber, bool isChapterStart = false) const
    {
        PageLayout resolved = *this;

        // Determine page type
        QString pageType;
        if (pageNumber == 0 || isChapterStart) {
            pageType = QStringLiteral("first");
        } else if (pageNumber % 2 == 0) {
            pageType = QStringLiteral("right"); // 0-based: even = right (pages 1,3,5...)
        } else {
            pageType = QStringLiteral("left");  // 0-based: odd = left (pages 2,4,6...)
        }

        auto it = masterPages.find(pageType);
        if (it == masterPages.end())
            return resolved;

        const MasterPage &mp = it.value();

        // Apply overrides
        if (mp.headerEnabled >= 0)
            resolved.headerEnabled = (mp.headerEnabled != 0);
        if (mp.footerEnabled >= 0)
            resolved.footerEnabled = (mp.footerEnabled != 0);
        if (mp.hasHeaderLeft)   resolved.headerLeft   = mp.headerLeft;
        if (mp.hasHeaderCenter) resolved.headerCenter = mp.headerCenter;
        if (mp.hasHeaderRight)  resolved.headerRight  = mp.headerRight;
        if (mp.hasFooterLeft)   resolved.footerLeft   = mp.footerLeft;
        if (mp.hasFooterCenter) resolved.footerCenter = mp.footerCenter;
        if (mp.hasFooterRight)  resolved.footerRight  = mp.footerRight;

        if (mp.marginTop >= 0 || mp.marginBottom >= 0
            || mp.marginLeft >= 0 || mp.marginRight >= 0) {
            resolved.margins = QMarginsF(
                mp.marginLeft >= 0   ? mp.marginLeft   : margins.left(),
                mp.marginTop >= 0    ? mp.marginTop    : margins.top(),
                mp.marginRight >= 0  ? mp.marginRight  : margins.right(),
                mp.marginBottom >= 0 ? mp.marginBottom : margins.bottom());
        }

        // Don't copy masterPages into resolved to avoid recursion
        resolved.masterPages.clear();

        return resolved;
    }
};

#endif // PENELOPE_PAGELAYOUT_H
