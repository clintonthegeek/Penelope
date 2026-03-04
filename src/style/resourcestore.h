/*
 * resourcestore.h — Common discovery/loading/saving logic for JSON resources
 *
 * Factored out of PaletteManager, PageTemplateManager, and TypeSetManager.
 * Not a QObject — the owning manager handles signals.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_RESOURCESTORE_H
#define PENELOPE_RESOURCESTORE_H

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <functional>

class ResourceStore
{
public:
    struct Entry {
        QString id;
        QString name;
        QString path;
        bool builtin = false;
    };

    using TypeChecker = std::function<bool(const QJsonObject &)>;

    /// Discover resources from a Qt resource dir and one or more user dirs.
    void discover(const QString &resourceDir,
                  const TypeChecker &matchesType,
                  const QStringList &userDirs)
    {
        m_userDir = userDirs.isEmpty() ? QString() : userDirs.first();
        m_entries.clear();

        // Built-in resources bundled as Qt resources
        scanDir(QStringLiteral(":/") + resourceDir, matchesType, true);

        // User resources from XDG data directories
        for (const QString &dirPath : userDirs)
            scanDir(dirPath, matchesType, false);
    }

    QStringList availableIds() const
    {
        QStringList ids;
        for (const auto &e : m_entries)
            ids.append(e.id);
        return ids;
    }

    QString name(const QString &id) const
    {
        for (const auto &e : m_entries) {
            if (e.id == id)
                return e.name;
        }
        return id;
    }

    bool isBuiltin(const QString &id) const
    {
        for (const auto &e : m_entries) {
            if (e.id == id)
                return e.builtin;
        }
        return false;
    }

    QJsonObject loadJson(const QString &id) const
    {
        for (const auto &e : m_entries) {
            if (e.id == id) {
                QFile file(e.path);
                if (!file.open(QIODevice::ReadOnly))
                    return {};
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                return doc.isNull() ? QJsonObject{} : doc.object();
            }
        }
        return {};
    }

    /// Save a resource. Returns the assigned ID, or empty on failure.
    QString save(const QString &itemId, const QString &itemName,
                 const QJsonObject &json, const QString &defaultBase)
    {
        if (m_userDir.isEmpty())
            return {};

        QDir().mkpath(m_userDir);

        QString id = itemId;
        if (id.isEmpty()) {
            QString base = itemName.toLower().replace(
                QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                QStringLiteral("-"));
            if (base.isEmpty())
                base = defaultBase;
            id = base;
            QString path = m_userDir + QLatin1Char('/') + id + QLatin1String(".json");
            int suffix = 1;
            while (QFile::exists(path)) {
                id = base + QLatin1Char('-') + QString::number(suffix++);
                path = m_userDir + QLatin1Char('/') + id + QLatin1String(".json");
            }
        }

        // Check for existing entry
        bool found = false;
        for (auto &e : m_entries) {
            if (e.id == id) {
                if (e.builtin)
                    return {};
                found = true;
                break;
            }
        }

        QString path = m_userDir + QLatin1Char('/') + id + QLatin1String(".json");

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};

        QJsonDocument doc(json);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();

        QString displayName = json.value(QLatin1String("name")).toString(id);
        if (!found) {
            m_entries.append({id, displayName, path, false});
        } else {
            for (auto &e : m_entries) {
                if (e.id == id) {
                    e.name = displayName;
                    e.path = path;
                    break;
                }
            }
        }

        return id;
    }

    bool remove(const QString &id, const char *managerName)
    {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].id == id) {
                if (m_entries[i].builtin)
                    return false;
                if (!QFile::remove(m_entries[i].path))
                    qWarning("%s: failed to remove %s",
                             managerName, qPrintable(m_entries[i].path));
                m_entries.removeAt(i);
                return true;
            }
        }
        return false;
    }

private:
    void scanDir(const QString &dirPath, const TypeChecker &matchesType, bool builtin)
    {
        QDir dir(dirPath);
        if (!dir.exists())
            return;

        const QStringList entries = dir.entryList(
            {QStringLiteral("*.json")}, QDir::Files);

        for (const QString &entry : entries) {
            QString path = dir.filePath(entry);
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;

            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isNull())
                continue;

            QJsonObject root = doc.object();
            if (!matchesType(root))
                continue;

            QString id = root.value(QLatin1String("id")).toString();
            if (id.isEmpty())
                id = QFileInfo(entry).completeBaseName();

            // Skip if already known (built-ins take precedence)
            bool alreadyKnown = false;
            for (const auto &existing : m_entries) {
                if (existing.id == id) { alreadyKnown = true; break; }
            }
            if (alreadyKnown)
                continue;

            QString name = root.value(QLatin1String("name")).toString(id);
            m_entries.append({id, name, path, builtin});
        }
    }

    QList<Entry> m_entries;
    QString m_userDir;
};

#endif // PENELOPE_RESOURCESTORE_H
