#ifndef PENELOPE_TOCWIDGET_H
#define PENELOPE_TOCWIDGET_H

#include <QHash>
#include <QWidget>

#include "contentmodel.h"
#include "layoutengine.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTextDocument;

class TocWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TocWidget(QWidget *parent = nullptr);

    void buildFromDocument(QTextDocument *document);
    void buildFromContentModel(const Content::Document &doc,
                               const QList<Layout::SourceMapEntry> &sourceMap);
    void clear();
    void highlightHeading(int sourceLine);

Q_SIGNALS:
    void headingClicked(int blockNumber);
    void headingNavigate(int page, qreal yOffset);
    void headingSourceNavigate(int sourceLine);

private Q_SLOTS:
    void onItemClicked(QTreeWidgetItem *item, int column);

private:
    QTreeWidget *m_treeWidget = nullptr;
    QHash<int, QTreeWidgetItem *> m_headingsByLine; // keyed by source startLine
};

#endif // PENELOPE_TOCWIDGET_H
