/*
 * fontdegradationmap.h — TTF/OTF to Hershey font family mapping
 *
 * Provides a static lookup table that maps common TTF/OTF font family
 * names to their closest Hershey vector-font counterpart.  Used as a
 * fallback when the TypeSet does not contain an explicit mapping
 * for a given family.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_FONTDEGRADATIONMAP_H
#define PENELOPE_FONTDEGRADATIONMAP_H

#include <QFont>
#include <QString>

class FontDegradationMap
{
public:
    /// Returns the best Hershey family name for the given TTF/OTF family.
    ///
    /// Uses a static table of known mappings first, then falls back to
    /// generic classification heuristics.  If nothing matches, returns
    /// "Hershey Sans" as the most neutral default.
    static QString hersheyFamilyFor(const QString &fontFamily);

    /// Classify a font family and return the appropriate QFont::StyleHint.
    ///
    /// Checks name patterns first (fast), then falls back to
    /// QFontDatabase::isFixedPitch() for monospace detection.
    static QFont::StyleHint guessStyleHint(const QString &fontFamily);

private:
    FontDegradationMap() = delete;
};

#endif // PENELOPE_FONTDEGRADATIONMAP_H
