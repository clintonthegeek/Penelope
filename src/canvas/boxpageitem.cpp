#include "boxpageitem.h"

#include "fontmanager.h"
#include "headerfooterplan.h"

#include <QFont>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

BoxPageItem::BoxPageItem(std::shared_ptr<const Layout::Page> page, QSizeF pageSize,
                         std::shared_ptr<FontManager> fonts, QGraphicsItem *parent)
    : QGraphicsItem(parent), m_page(std::move(page)), m_pageSize(pageSize),
      m_fonts(std::move(fonts)), m_renderer(m_fonts.get())
{
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption);
    if (m_page) {
        m_hitIndex = buildHitIndex(*m_page);
        m_totalPages = qMax(1, m_page->pageNumber + 1);
    }
}

void BoxPageItem::setPageChrome(const PageLayout &layout, int totalPages,
                                const QString &fileName, const QString &title)
{
    m_layout = layout;
    m_totalPages = qMax(1, totalPages);
    m_fileName = fileName;
    m_title = title;
    update();
}

QRectF BoxPageItem::boundingRect() const
{
    constexpr qreal shadow = 4.0;
    return QRectF(-1, -1, m_pageSize.width() + shadow + 2, m_pageSize.height() + shadow + 2);
}

QPointF BoxPageItem::contentOrigin() const
{
    constexpr qreal mmToPt = 72.0 / 25.4;
    return QPointF(m_layout.margins.left() * mmToPt,
                   m_layout.margins.top() * mmToPt + m_layout.headerTotalHeight());
}

QPointF BoxPageItem::toContent(QPointF itemPos) const
{
    return itemPos - contentOrigin();
}

QString BoxPageItem::linkAt(QPointF itemPos) const
{
    const QPointF contentPos = toContent(itemPos);
    for (int i = 0; i < m_hitIndex.linkBounds.size(); ++i) {
        if (m_hitIndex.linkBounds.at(i).contains(contentPos))
            return m_hitIndex.linkHrefs.at(i);
    }
    return {};
}

QRectF BoxPageItem::lineRect(int index) const
{
    if (index < 0 || index >= m_hitIndex.lineBounds.size())
        return {};
    return m_hitIndex.lineBounds.at(index).translated(contentOrigin());
}

QString BoxPageItem::textInRect(const QRectF &itemRect) const
{
    const QRectF contentRect = itemRect.translated(-contentOrigin());
    QString text;
    for (int i = 0; i < m_hitIndex.lineBounds.size(); ++i) {
        if (!m_hitIndex.lineBounds.at(i).intersects(contentRect))
            continue;
        if (!text.isEmpty())
            text += QLatin1Char(' ');
        text += m_hitIndex.lineText.at(i);
    }
    return text;
}

void BoxPageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                        QWidget * /*widget*/)
{
    Q_UNUSED(option);
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 50));
    painter->drawRect(QRectF(4, 4, m_pageSize.width(), m_pageSize.height()));
    painter->fillRect(QRectF(0, 0, m_pageSize.width(), m_pageSize.height()),
                      m_layout.pageBackground.isValid() ? m_layout.pageBackground : QColor(Qt::white));
    painter->setBrush(Qt::NoBrush);

    const int pageNumber = m_page ? m_page->pageNumber : 0;
    const HeaderFooterPlan plan = makeHeaderFooterPlan(m_layout, pageNumber, m_totalPages, m_fileName, m_title);
    QFont font(QStringLiteral("Noto Sans"));
    font.setPointSizeF(plan.fontSize);
    painter->setFont(font);
    painter->setPen(QColor(0x88, 0x88, 0x88));
    auto drawField = [&](const HeaderFooterField &field, Qt::Alignment align) {
        if (!field.text.isEmpty())
            painter->drawText(field.rect, int(align) | Qt::AlignVCenter, field.text);
    };
    if (plan.headerEnabled) {
        drawField(plan.headerLeft, Qt::AlignLeft);
        drawField(plan.headerCenter, Qt::AlignHCenter);
        drawField(plan.headerRight, Qt::AlignRight);
    }
    if (plan.footerEnabled) {
        drawField(plan.footerLeft, Qt::AlignLeft);
        drawField(plan.footerCenter, Qt::AlignHCenter);
        drawField(plan.footerRight, Qt::AlignRight);
    }

    painter->translate(contentOrigin());
    m_renderer.setPainter(painter);
    m_renderer.setLineClip(QRectF());
    if (m_page) {
        for (const auto &element : m_page->elements)
            m_renderer.renderElement(element);
    }
    painter->restore();
}
