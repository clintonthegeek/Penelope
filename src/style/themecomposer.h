/*
 * themecomposer.h — Compose ColorPalette + TypeSet into a StyleManager
 *
 * Applies semantic color roles and typographic roles to the style
 * hierarchy, keeping palette/type set files portable.  The role-to-style
 * mapping is encapsulated here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_THEMECOMPOSER_H
#define PENELOPE_THEMECOMPOSER_H

#include <QObject>

#include "colorpalette.h"
#include "typeset.h"

class StyleManager;
class ThemeManager;

class ThemeComposer : public QObject
{
    Q_OBJECT

public:
    explicit ThemeComposer(ThemeManager *themeManager, QObject *parent = nullptr);

    void setColorPalette(const ColorPalette &palette);
    void setTypeSet(const TypeSet &typeSet);

    /// Compose the current type set + palette into @p target.
    ///
    /// Composition order:
    ///   1. loadDefaults() — hardcoded style hierarchy
    ///   2. Type set (fonts + style overrides)
    ///   3. Color palette — set foreground/background per the role mapping
    ///   4. assignDefaultParents() — ensure parent hierarchy is intact
    void compose(StyleManager *target);

    const ColorPalette &currentPalette() const { return m_palette; }
    const TypeSet &currentTypeSet() const { return m_typeSet; }

    /// Look up the Hershey fallback for a TTF family.
    /// Dispatches to whichever typography source is active.
    QString hersheyFamilyFor(const QString &ttfFamily) const;

private:
    void applyTypeSet(StyleManager *target);
    void applyColorPalette(StyleManager *target);

    ThemeManager *m_themeManager = nullptr;
    ColorPalette m_palette;
    TypeSet m_typeSet;
};

#endif // PENELOPE_THEMECOMPOSER_H
