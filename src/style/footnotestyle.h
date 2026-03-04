#ifndef PENELOPE_FOOTNOTESTYLE_H
#define PENELOPE_FOOTNOTESTYLE_H

#include <QString>

struct FootnoteStyle
{
    enum NumberFormat { Arabic, RomanLower, RomanUpper, AlphaLower, AlphaUpper, Asterisk };
    enum RestartMode { PerDocument, PerPage };

    NumberFormat format = Arabic;
    int startNumber = 1;
    RestartMode restart = PerDocument;
    QString prefix;
    QString suffix;
    bool superscriptRef = true;      // in body text
    bool superscriptNote = false;    // in note area
    bool asEndnotes = true;          // endnotes (true) vs footnotes (false)

    bool showSeparator = true;
    qreal separatorWidth = 0.5;      // pt
    qreal separatorLength = 72.0;    // pt

    QString formatNumber(int n) const;
};

#endif // PENELOPE_FOOTNOTESTYLE_H
