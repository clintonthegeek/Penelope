/*
 * typeset.h — Typography axis of the three-axis theme system
 *
 * Bundles font roles (body, heading, mono), sizing, spacing, and
 * all non-color style overrides.  Combined with a ColorPalette and
 * PageTemplate by ThemeComposer to produce the final styled document.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_TYPESET_H
#define PENELOPE_TYPESET_H

#include <QJsonObject>
#include <QString>

struct FontRole {
    QString family;        // e.g. "Noto Serif"
    QString hersheyFamily; // e.g. "Hershey Serif"

    bool operator==(const FontRole &other) const {
        return family == other.family && hersheyFamily == other.hersheyFamily;
    }
};

class TypeSet
{
public:
    TypeSet() = default;

    QString id;          // kebab-case, e.g. "default"
    QString name;        // display name
    QString description;
    int version = 1;
    bool hersheyMode = false;

    FontRole body;
    FontRole heading;
    FontRole mono;

    // Style overrides as raw JSON — applied by ThemeManager::applyStyleOverrides
    QJsonObject paragraphStyles;
    QJsonObject characterStyles;
    QJsonObject tableStyles;
    QJsonObject footnoteStyle;

    /// Look up the Hershey fallback family for a given TTF/OTF family.
    QString hersheyFamilyFor(const QString &ttfFamily) const;

    bool operator==(const TypeSet &other) const {
        return id == other.id && body == other.body
               && heading == other.heading && mono == other.mono;
    }

    static TypeSet fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};

#endif // PENELOPE_TYPESET_H
