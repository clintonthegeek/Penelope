/*
 * palettemanager.h — Discovery/loading/saving for color palettes
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PALETTEMANAGER_H
#define PENELOPE_PALETTEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "colorpalette.h"
#include "resourcestore.h"

class PaletteManager : public QObject
{
    Q_OBJECT

public:
    explicit PaletteManager(QObject *parent = nullptr);

    QStringList availablePalettes() const { return m_store.availableIds(); }
    QString paletteName(const QString &id) const { return m_store.name(id); }
    ColorPalette palette(const QString &id) const;
    QString savePalette(const ColorPalette &palette);
    bool deletePalette(const QString &id);
    bool isBuiltin(const QString &id) const { return m_store.isBuiltin(id); }

Q_SIGNALS:
    void palettesChanged();

private:
    ResourceStore m_store;
};

#endif // PENELOPE_PALETTEMANAGER_H
