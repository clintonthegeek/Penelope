#include "metadatastore.h"
#include "penelopesettings.h"
#include <QDateTime>
#include <QFileInfo>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSaveFile>

MetadataStore::MetadataStore(QObject *parent)
    : QObject(parent)
{
}

QString MetadataStore::metadataDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + QStringLiteral("/metadata");
    QDir().mkpath(dir);
    return dir;
}

QString MetadataStore::hashPath(const QString &filePath) const
{
    QByteArray hash = QCryptographicHash::hash(
        filePath.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex().left(16));
}

QString MetadataStore::metadataFilePath(const QString &filePath) const
{
    return metadataDir() + QLatin1Char('/') + hashPath(filePath)
           + QStringLiteral(".json");
}

QJsonObject MetadataStore::load(const QString &filePath) const
{
    if (!PenelopeSettings::self()->rememberPerFileSettings()) return {};
    QFile file(metadataFilePath(filePath));
    const int expiry = PenelopeSettings::self()->metaInfoExpiryDays();
    if (expiry > 0 && QFileInfo(file).lastModified().daysTo(QDateTime::currentDateTime()) >= expiry) {
        file.remove();
        return {};
    }
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

void MetadataStore::save(const QString &filePath, const QJsonObject &metadata)
{
    if (!PenelopeSettings::self()->rememberPerFileSettings()) return;
    QSaveFile file(metadataFilePath(filePath));
    if (!file.open(QIODevice::WriteOnly))
        return;

    // Store the original file path in metadata for identification
    QJsonObject obj = metadata;
    obj[QStringLiteral("_filePath")] = filePath;

    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(data) == data.size()) file.commit();
}

void MetadataStore::setValue(const QString &filePath, const QString &key,
                              const QJsonValue &value)
{
    QJsonObject obj = load(filePath);
    obj[key] = value;
    save(filePath, obj);
}

QJsonValue MetadataStore::value(const QString &filePath, const QString &key,
                                 const QJsonValue &defaultValue) const
{
    QJsonObject obj = load(filePath);
    if (obj.contains(key))
        return obj[key];
    return defaultValue;
}
