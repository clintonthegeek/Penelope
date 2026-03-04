/*
 * pagetemplate.h — Page layout template for the three-axis theme system
 *
 * Bundles page size, margins, header/footer configuration, and master
 * page definitions.  Combined with a ColorPalette and TypeSet by the
 * theme picker to produce the final document layout.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PAGETEMPLATE_H
#define PENELOPE_PAGETEMPLATE_H

#include <QJsonObject>
#include <QString>

#include "pagelayout.h"

struct PageTemplate
{
    QString id;          // kebab-case, e.g. "default"
    QString name;        // display name
    QString description;
    int version = 1;

    PageLayout pageLayout;  // everything except pageBackground (palette owns that)

    static PageTemplate fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};

#endif // PENELOPE_PAGETEMPLATE_H
