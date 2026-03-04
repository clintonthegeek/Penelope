#ifndef PENELOPE_THEMEMANAGER_H
#define PENELOPE_THEMEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QJsonObject;
class StyleManager;

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    explicit ThemeManager(QObject *parent = nullptr);

    // Create a default StyleManager with built-in defaults
    void loadDefaults(StyleManager *styleManager);

    // Ensure parent hierarchy is intact after external modifications
    void assignDefaultParents(StyleManager *sm);

    // Apply style overrides from a JSON object to a StyleManager
    void applyStyleOverrides(const QJsonObject &root, StyleManager *sm);

private:
    void resolveAllStyles(StyleManager *sm);
};

#endif // PENELOPE_THEMEMANAGER_H
