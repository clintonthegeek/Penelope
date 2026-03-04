/*
 * colorpalette.h — Semantic color palette for theme composition
 *
 * Maps semantic color roles (text, headings, backgrounds, borders)
 * to QColor values.  Used as one of the two independent axes of the
 * theme system (the other being TypeSet).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_COLORPALETTE_H
#define PENELOPE_COLORPALETTE_H

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QString>

class ColorPalette
{
public:
    ColorPalette() = default;

    QString id;          // kebab-case identifier, e.g. "default-light"
    QString name;        // display name, e.g. "Default Light"
    QString description;

    QHash<QString, QColor> colors; // role -> color

    // --- Convenience accessors (return colors.value(role) with fallback) ---

    QColor text() const;
    QColor headingText() const;
    QColor blockquoteText() const;
    QColor linkText() const;
    QColor codeText() const;

    QColor surfaceCode() const;
    QColor surfaceInlineCode() const;
    QColor surfaceTableHeader() const;
    QColor surfaceTableAlt() const;

    QColor pageBackground() const;

    QColor borderOuter() const;
    QColor borderInner() const;
    QColor borderHeaderBottom() const;

    // --- Utilities ---

    /// Returns true if pageBackground or any surface* role is fully opaque
    /// and not white (i.e. the palette specifies visually distinct backgrounds).
    bool hasNonWhiteBackgrounds() const;

    bool operator==(const ColorPalette &other) const {
        return id == other.id && colors == other.colors;
    }

    // --- Serialization ---

    static ColorPalette fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};

#endif // PENELOPE_COLORPALETTE_H
