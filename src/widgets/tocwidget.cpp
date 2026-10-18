#include "tocwidget.h"

#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

TocWidget::TocWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setIndentation(16);
    layout->addWidget(m_treeWidget);

    connect(m_treeWidget, &QTreeWidget::itemClicked,
            this, &TocWidget::onItemClicked);
}

void TocWidget::buildFromContentModel(const Content::Document &doc,
                                       const QList<Layout::SourceMapEntry> &sourceMap)
{
    m_treeWidget->blockSignals(true);
    m_treeWidget->clear();
    m_headingsByLine.clear();

    QTreeWidgetItem *parents[7] = {};

    for (const auto &block : doc.blocks) {
        const Content::Heading *heading = std::get_if<Content::Heading>(&block);
        if (!heading)
            continue;

        int level = heading->level;
        if (level < 1 || level > 6)
            continue;

        // Extract heading text from inlines
        QString text;
        for (const auto &node : heading->inlines) {
            std::visit([&](const auto &n) {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Content::TextRun>)
                    text += n.text;
                else if constexpr (std::is_same_v<T, Content::InlineCode>)
                    text += n.text;
                else if constexpr (std::is_same_v<T, Content::Link>)
                    text += n.text;
                else if constexpr (std::is_same_v<T, Content::Math>)
                    text += n.source;
            }, node);
        }
        text = text.trimmed();
        if (text.isEmpty())
            continue;

        // Find page and y-offset from source map. A heading that has not been
        // laid out yet has no geometry; y = 0 would mean the top of the document.
        int page = -1;
        qreal yOffset = 0;
        if (heading->source.startLine > 0) {
            for (const auto &entry : sourceMap) {
                if (entry.startLine == heading->source.startLine
                    && entry.endLine == heading->source.endLine) {
                    page = entry.pageNumber;
                    yOffset = entry.rect.top();
                    break;
                }
            }
        }

        auto *item = new QTreeWidgetItem();
        item->setText(0, text);
        // Store page in UserRole, y-offset in UserRole+1, source line in UserRole+2
        item->setData(0, Qt::UserRole, page);
        item->setData(0, Qt::UserRole + 1, yOffset);
        item->setData(0, Qt::UserRole + 2, heading->source.startLine);

        // Find the appropriate parent
        QTreeWidgetItem *parent = nullptr;
        for (int i = level - 1; i >= 1; --i) {
            if (parents[i]) {
                parent = parents[i];
                break;
            }
        }

        if (parent) {
            parent->addChild(item);
        } else {
            m_treeWidget->addTopLevelItem(item);
        }

        parents[level] = item;
        if (heading->source.startLine > 0)
            m_headingsByLine.insert(heading->source.startLine, item);
        for (int i = level + 1; i <= 6; ++i)
            parents[i] = nullptr;
    }

    m_treeWidget->expandAll();
    applyHighlight();
    m_treeWidget->blockSignals(false);
}

void TocWidget::clear()
{
    m_highlightedLine = -1;
    m_treeWidget->clear();
    m_headingsByLine.clear();
}

void TocWidget::onItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    const int sourceLine = item->data(0, Qt::UserRole + 2).toInt();
    const int page = item->data(0, Qt::UserRole).toInt();
    const qreal yOffset = item->data(0, Qt::UserRole + 1).toDouble();
    Q_EMIT headingNavigate(page, yOffset, sourceLine);
    if (sourceLine > 0) Q_EMIT headingSourceNavigate(sourceLine);

}

void TocWidget::highlightHeading(int sourceLine)
{
    m_highlightedLine = sourceLine;
    applyHighlight();
}

void TocWidget::applyHighlight()
{
    auto it = m_headingsByLine.find(m_highlightedLine);
    QTreeWidgetItem *target = (it != m_headingsByLine.end()) ? *it : nullptr;
    if (m_treeWidget->currentItem() == target)
        return;
    m_treeWidget->setCurrentItem(target);
    if (target)
        m_treeWidget->scrollToItem(target);
}
