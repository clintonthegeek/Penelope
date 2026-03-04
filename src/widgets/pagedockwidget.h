// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_PAGEDOCKWIDGET_H
#define PENELOPE_PAGEDOCKWIDGET_H

#include <QWidget>

#include "pagelayout.h"

class ItemSelectorBar;
class PageLayoutWidget;
class PageTemplateManager;

class PageDockWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PageDockWidget(PageTemplateManager *templateManager,
                            QWidget *parent = nullptr);

    PageLayout currentPageLayout() const;
    void setPageLayout(const PageLayout &layout);

    void setCurrentTemplateId(const QString &id);
    QString currentTemplateId() const;

Q_SIGNALS:
    void pageLayoutChanged();
    void templateChanged(const QString &id);

private Q_SLOTS:
    void onTemplateSelectionChanged(const QString &id);
    void onDuplicate();
    void onSave();
    void onDelete();

private:
    void buildUI();
    void populateSelector();

    PageTemplateManager *m_templateManager = nullptr;

    ItemSelectorBar *m_selectorBar = nullptr;
    PageLayoutWidget *m_pageLayoutWidget = nullptr;
};

#endif // PENELOPE_PAGEDOCKWIDGET_H
