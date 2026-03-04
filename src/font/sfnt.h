/*
 * sfnt.h — TrueType/OpenType font subsetting via HarfBuzz
 *
 * Extracted from Scribus fonts/sfnt.h (Andreas Vox, 2015) and simplified.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_SFNT_H
#define PENELOPE_SFNT_H

#include <QByteArray>
#include <QList>
#include <QMap>

namespace sfnt {

struct SubsetResult {
    QByteArray fontData;
    QMap<uint, uint> glyphMap; // original glyph ID → subset glyph ID
    bool success = false;
};

SubsetResult subsetFace(const QByteArray &fontData, const QList<uint> &glyphIds,
                        int faceIndex = 0);

} // namespace sfnt

#endif // PENELOPE_SFNT_H
