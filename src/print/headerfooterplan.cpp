#include "headerfooterplan.h"

#include "headerfooterrenderer.h"

namespace {

bool fieldUsesPageToken(const QString &text)
{
    return text.contains(QLatin1String("{page}")) || text.contains(QLatin1String("{pages}"));
}

QRectF band(qreal x, qreal y, qreal width, qreal height, int slot)
{
    const qreal slice = width / 3.0;
    return QRectF(x + slice * slot, y, slice, height);
}

} // namespace

HeaderFooterPlan makeHeaderFooterPlan(const PageLayout &pageLayout,
                                       int pageNumber,
                                       int totalPages,
                                       const QString &fileName,
                                       const QString &title)
{
    const PageLayout resolved = pageLayout.resolvedForPage(pageNumber);
    const PageMetadata meta{pageNumber, totalPages, fileName, title};
    HeaderFooterPlan plan;
    plan.fontSize = 9.0;
    plan.headerEnabled = resolved.headerEnabled;
    plan.footerEnabled = resolved.footerEnabled;
    const QSizeF pageSize = pageLayout.pageSizePoints();
    plan.pageRect = QRectF(QPointF(0, 0), pageSize);

    constexpr qreal mmToPt = 72.0 / 25.4;
    const qreal marginLeft = pageLayout.margins.left() * mmToPt;
    const qreal marginRight = pageLayout.margins.right() * mmToPt;
    const qreal marginTop = pageLayout.margins.top() * mmToPt;
    const qreal marginBottom = pageLayout.margins.bottom() * mmToPt;
    const qreal contentWidth = qMax<qreal>(0, pageSize.width() - marginLeft - marginRight);

    plan.headerLeft.text = HeaderFooterRenderer::resolveField(resolved.headerLeft, meta);
    plan.headerCenter.text = HeaderFooterRenderer::resolveField(resolved.headerCenter, meta);
    plan.headerRight.text = HeaderFooterRenderer::resolveField(resolved.headerRight, meta);
    plan.footerLeft.text = HeaderFooterRenderer::resolveField(resolved.footerLeft, meta);
    plan.footerCenter.text = HeaderFooterRenderer::resolveField(resolved.footerCenter, meta);
    plan.footerRight.text = HeaderFooterRenderer::resolveField(resolved.footerRight, meta);

    const qreal headerY = qMax<qreal>(0, marginTop - PageLayout::kHeaderHeight);
    plan.headerLeft.rect = band(marginLeft, headerY, contentWidth, PageLayout::kHeaderHeight, 0);
    plan.headerCenter.rect = band(marginLeft, headerY, contentWidth, PageLayout::kHeaderHeight, 1);
    plan.headerRight.rect = band(marginLeft, headerY, contentWidth, PageLayout::kHeaderHeight, 2);

    const qreal footerY = pageSize.height() - marginBottom;
    plan.footerLeft.rect = band(marginLeft, footerY, contentWidth, PageLayout::kFooterHeight, 0);
    plan.footerCenter.rect = band(marginLeft, footerY, contentWidth, PageLayout::kFooterHeight, 1);
    plan.footerRight.rect = band(marginLeft, footerY, contentWidth, PageLayout::kFooterHeight, 2);
    return plan;
}

bool headerFooterUsesPageToken(const PageLayout &pageLayout)
{
    if (pageLayout.headerEnabled
        && (fieldUsesPageToken(pageLayout.headerLeft)
            || fieldUsesPageToken(pageLayout.headerCenter)
            || fieldUsesPageToken(pageLayout.headerRight)))
        return true;
    if (pageLayout.footerEnabled
        && (fieldUsesPageToken(pageLayout.footerLeft)
            || fieldUsesPageToken(pageLayout.footerCenter)
            || fieldUsesPageToken(pageLayout.footerRight)))
        return true;
    return false;
}
