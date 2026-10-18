#include "mathrenderer.h"

#include <QCryptographicHash>
#include <QPainter>

#include <jkqtmathtext/jkqtmathtext.h>

#include <cmath>

namespace Layout {

MathArt typesetMath(const QString &latex, qreal pixelSize, const QColor &color)
{
    MathArt art;
    if (latex.trimmed().isEmpty() || pixelSize <= 0)
        return art;

    JKQTMathText math;
    math.useXITS();
    math.setFontSizePixels(pixelSize);
    math.setFontColor(color.isValid() ? color : QColor(0x1a, 0x1a, 0x1a));
    if (!math.parse(latex))
        return art;

    // Measuring needs a painter; the probe never leaves this function.
    QImage probe(1, 1, QImage::Format_ARGB32_Premultiplied);
    QPainter measure(&probe);
    qreal width = 0;
    qreal ascent = 0;
    qreal descent = 0;
    qreal strikeout = 0;
    math.getSizeDetail(measure, width, ascent, descent, strikeout);
    measure.end();

    if (width <= 0 || (ascent + descent) <= 0)
        return art;

    const qreal padding = 1.0;
    const int pixelWidth = static_cast<int>(std::ceil(width + 2 * padding));
    const int pixelHeight = static_cast<int>(std::ceil(ascent + descent + 2 * padding));
    if (pixelWidth <= 0 || pixelHeight <= 0)
        return art;

    QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        // draw() takes the baseline, so the padding sits above the formula's
        // own ascent and the image grows by the same amount below.
        math.draw(painter, QPointF(padding, padding + ascent));
    }

    art.image = image;
    art.width = image.width();
    art.ascent = ascent + padding;
    art.descent = image.height() - art.ascent;
    const QByteArray digest = QCryptographicHash::hash(
        latex.toUtf8() + QByteArray::number(pixelSize) + color.name().toUtf8(),
        QCryptographicHash::Md5).toHex();
    art.id = QStringLiteral("math-%1").arg(QString::fromLatin1(digest));
    return art;
}

} // namespace Layout
