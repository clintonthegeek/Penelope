#ifndef PENELOPE_SMARTPUNCTUATION_H
#define PENELOPE_SMARTPUNCTUATION_H

#include <QString>

// Typographic prose transforms: curly quotes, en and em dashes, ellipses.
// Runs on normal text only; code spans, code blocks and entity references are
// never passed through it. The context decides each quote's direction, so the
// result does not depend on how the text was split into runs.
QString applySmartPunctuation(const QString &text);

#endif // PENELOPE_SMARTPUNCTUATION_H
