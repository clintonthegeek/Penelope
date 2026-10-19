// SPDX-License-Identifier: GPL-2.0-or-later

#include "pagedockwidget.h"
#include "itemselectorbar.h"
#include "pagelayoutwidget.h"
#include "pagetemplate.h"
#include "pagetemplatemanager.h"

#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

PageDockWidget::PageDockWidget(PageTemplateManager *templateManager,
                               QWidget *parent)
    : QWidget(parent)
    , m_templateManager(templateManager)
{
    buildUI();
    populateSelector();

    connect(m_templateManager, &PageTemplateManager::templatesChanged,
            this, &PageDockWidget::populateSelector);
}

void PageDockWidget::buildUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // --- Template Selector ---
    m_selectorBar = new ItemSelectorBar(this);
    layout->addWidget(m_selectorBar);

    connect(m_selectorBar, &ItemSelectorBar::currentItemChanged,
            this, &PageDockWidget::onTemplateSelectionChanged);
    connect(m_selectorBar, &ItemSelectorBar::duplicateRequested,
            this, &PageDockWidget::onDuplicate);
    connect(m_selectorBar, &ItemSelectorBar::saveRequested,
            this, &PageDockWidget::onSave);
    connect(m_selectorBar, &ItemSelectorBar::deleteRequested,
            this, &PageDockWidget::onDelete);

    // --- Page Layout Widget ---
    m_pageLayoutWidget = new PageLayoutWidget(this);
    layout->addWidget(m_pageLayoutWidget, 1);

    connect(m_pageLayoutWidget, &PageLayoutWidget::pageLayoutChanged,
            this, &PageDockWidget::pageLayoutChanged);

    // A builtin template is read-only: edits change this document only.
    m_templateHint = new QLabel;
    m_templateHint->setWordWrap(true);
    m_templateHint->setTextFormat(Qt::RichText);
    m_templateHint->setVisible(false);
    layout->addWidget(m_templateHint);
}

void PageDockWidget::populateSelector()
{
    const QStringList ids = m_templateManager->availableTemplates();
    QStringList names;
    QStringList builtinIds;
    for (const QString &id : ids) {
        names.append(m_templateManager->templateName(id));
        if (m_templateManager->isBuiltin(id))
            builtinIds.append(id);
    }
    m_selectorBar->setItems(ids, names, builtinIds);
}

PageLayout PageDockWidget::currentPageLayout() const
{
    return m_pageLayoutWidget->currentPageLayout();
}

void PageDockWidget::setPageLayout(const PageLayout &layout)
{
    m_pageLayoutWidget->setPageLayout(layout);
}

void PageDockWidget::setCurrentTemplateId(const QString &id)
{
    m_selectorBar->setCurrentId(id);
    // Load the template's page layout into the widget
    PageTemplate tmpl = m_templateManager->pageTemplate(id);
    if (!tmpl.id.isEmpty())
        m_pageLayoutWidget->setPageLayout(tmpl.pageLayout);
}

QString PageDockWidget::currentTemplateId() const
{
    return m_selectorBar->currentId();
}

void PageDockWidget::onTemplateSelectionChanged(const QString &id)
{
    PageTemplate tmpl = m_templateManager->pageTemplate(id);
    if (!tmpl.id.isEmpty()) {
        m_pageLayoutWidget->setPageLayout(tmpl.pageLayout);
        updateTemplateHint();
        Q_EMIT templateChanged(id);
    }
}

void PageDockWidget::updateTemplateHint()
{
    if (!m_templateHint)
        return;
    const QString id = m_selectorBar->currentId();
    const bool builtin = !id.isEmpty() && m_templateManager->isBuiltin(id);
    if (builtin) {
        m_templateHint->setText(tr("<i>%1</i> is built in: changes apply to this document. "
                                   "Use Duplicate to keep them.")
                                    .arg(m_templateManager->templateName(id).toHtmlEscaped()));
    }
    m_templateHint->setVisible(builtin);
}

void PageDockWidget::onDuplicate()
{
    QString srcId = m_selectorBar->currentId();
    PageTemplate tmpl = m_templateManager->pageTemplate(srcId);
    if (tmpl.id.isEmpty())
        return;

    tmpl.id.clear();
    tmpl.name = tr("Copy of %1").arg(tmpl.name);
    // Capture current layout edits into the duplicate
    tmpl.pageLayout = m_pageLayoutWidget->currentPageLayout();
    QString newId = m_templateManager->saveTemplate(tmpl);
    m_selectorBar->setCurrentId(newId);
}

void PageDockWidget::onSave()
{
    QString id = m_selectorBar->currentId();
    if (id.isEmpty() || m_templateManager->isBuiltin(id))
        return;

    PageTemplate tmpl = m_templateManager->pageTemplate(id);
    tmpl.pageLayout = m_pageLayoutWidget->currentPageLayout();
    m_templateManager->saveTemplate(tmpl);
}

void PageDockWidget::onDelete()
{
    QString id = m_selectorBar->currentId();
    if (id.isEmpty() || m_templateManager->isBuiltin(id))
        return;

    int ret = QMessageBox::question(this, tr("Delete Template"),
                                    tr("Delete \"%1\"?").arg(m_templateManager->templateName(id)),
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    m_templateManager->deleteTemplate(id);
    const QStringList ids = m_templateManager->availableTemplates();
    if (!ids.isEmpty()) {
        m_selectorBar->setCurrentId(ids.first());
        onTemplateSelectionChanged(ids.first());
    }
}
