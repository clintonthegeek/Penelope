/*
 * pagetemplatemanager.h — Discovery/loading/saving for page templates
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PAGETEMPLATEMANAGER_H
#define PENELOPE_PAGETEMPLATEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "pagetemplate.h"
#include "resourcestore.h"

class PageTemplateManager : public QObject
{
    Q_OBJECT

public:
    explicit PageTemplateManager(QObject *parent = nullptr);

    QStringList availableTemplates() const { return m_store.availableIds(); }
    QString templateName(const QString &id) const { return m_store.name(id); }
    PageTemplate pageTemplate(const QString &id) const;
    QString saveTemplate(const PageTemplate &tmpl);
    bool deleteTemplate(const QString &id);
    bool isBuiltin(const QString &id) const { return m_store.isBuiltin(id); }

Q_SIGNALS:
    void templatesChanged();

private:
    ResourceStore m_store;
};

#endif // PENELOPE_PAGETEMPLATEMANAGER_H
