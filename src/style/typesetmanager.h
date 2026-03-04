/*
 * typesetmanager.h — Discovery/loading/saving for type sets
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_TYPESETMANAGER_H
#define PENELOPE_TYPESETMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "typeset.h"
#include "resourcestore.h"

class TypeSetManager : public QObject
{
    Q_OBJECT

public:
    explicit TypeSetManager(QObject *parent = nullptr);

    QStringList availableTypeSets() const { return m_store.availableIds(); }
    QString typeSetName(const QString &id) const { return m_store.name(id); }
    TypeSet typeSet(const QString &id) const;
    QString saveTypeSet(const TypeSet &typeSet);
    bool deleteTypeSet(const QString &id);
    bool isBuiltin(const QString &id) const { return m_store.isBuiltin(id); }

Q_SIGNALS:
    void typeSetsChanged();

private:
    ResourceStore m_store;
};

#endif // PENELOPE_TYPESETMANAGER_H
