#include "boxpageitem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

BoxPageItem::BoxPageItem(std::shared_ptr<const Layout::Page> page, QSizeF pageSize,
                         std::shared_ptr<FontManager> fonts, QGraphicsItem *parent)
    : QGraphicsItem(parent), m_page(std::move(page)), m_pageSize(pageSize),
      m_fonts(std::move(fonts)), m_renderer(m_fonts.get())
{
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption);
}

QRectF BoxPageItem::boundingRect() const
{
    return QRectF(0, 0, m_pageSize.width() + 4, m_pageSize.height() + 4);
}

void BoxPageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                        QWidget * /*widget*/)
{
    Q_UNUSED(option);
    painter->fillRect(QRectF(0, 0, m_pageSize.width(), m_pageSize.height()), Qt::white);
    m_renderer.setPainter(painter);
    if (m_page) {
        for (const auto &el : m_page->elements) {
            m_renderer.renderElement(el);
        }
    }
}
