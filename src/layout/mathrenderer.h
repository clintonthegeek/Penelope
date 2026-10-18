#ifndef PENELOPE_MATHRENDERER_H
#define PENELOPE_MATHRENDERER_H

#include <QColor>
#include <QImage>
#include <QString>

namespace Layout {

// A formula typeset into a raster, sized in layout units. The image sits in the
// text flow like a glyph box: `ascent` above the baseline, `descent` below.
struct MathArt {
    QImage image;
    qreal width = 0;
    qreal ascent = 0;
    qreal descent = 0;
    QString id; // resource name, so the PDF writer can embed it

    bool isValid() const { return !image.isNull() && width > 0; }
};

// Typesets LaTeX with jkqtmathtext, whose XITS and Fira Math faces are compiled
// into the vendored library. Returns an invalid MathArt when the source does
// not parse, so callers can leave the formula as plain text.
MathArt typesetMath(const QString &latex, qreal pixelSize, const QColor &color);

} // namespace Layout

#endif // PENELOPE_MATHRENDERER_H
