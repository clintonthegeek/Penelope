#ifndef PENELOPE_METADATASTORE_H
#define PENELOPE_METADATASTORE_H

#include <QJsonObject>
#include <QObject>
#include <QString>

class MetadataStore : public QObject
{
    Q_OBJECT

public:
    explicit MetadataStore(QObject *parent = nullptr);

    // Load/save metadata for a specific file path
    QJsonObject load(const QString &filePath) const;
    void save(const QString &filePath, const QJsonObject &metadata);

    // Convenience: store a single key
    void setValue(const QString &filePath, const QString &key,
                  const QJsonValue &value);
    QJsonValue value(const QString &filePath, const QString &key,
                     const QJsonValue &defaultValue = {}) const;

private:
    QString metadataDir() const;
    QString metadataFilePath(const QString &filePath) const;
    QString hashPath(const QString &filePath) const;
};

#endif // PENELOPE_METADATASTORE_H
