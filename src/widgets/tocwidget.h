#ifndef PENELOPE_TOCWIDGET_H
#define PENELOPE_TOCWIDGET_H

#include <QHash>
#include <QWidget>

#include "contentmodel.h"
#include "layoutengine.h"

class QTreeWidget;
class QTreeWidgetItem;

class TocWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TocWidget(QWidget *parent = nullptr);

    void buildFromContentModel(const Content::Document &doc,
                               const QList<Layout::SourceMapEntry> &sourceMap);
    void clear();
    void highlightHeading(int sourceLine);

Q_SIGNALS:
    void headingNavigate(int page, qreal yOffset, int sourceLine);
    void headingSourceNavigate(int sourceLine);

private Q_SLOTS:
    void onItemClicked(QTreeWidgetItem *item, int column);

private:
    void applyHighlight();

    QTreeWidget *m_treeWidget = nullptr;
    QHash<int, QTreeWidgetItem *> m_headingsByLine; // keyed by source startLine
    int m_highlightedLine = -1;
};

#endif // PENELOPE_TOCWIDGET_H
