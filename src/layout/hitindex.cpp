#include "hitindex.h"

#include <limits>

namespace {

void addLine(HitIndex &index, const Layout::BlockBox &box, const Layout::LineBox &line, qreal lineY)
{
    const qreal width = line.width > 0 ? line.width : box.width;
    QRectF bounds(box.x + line.x, box.y + lineY, qMax(width, qreal(1)), qMax(line.height, qreal(1)));
    index.lineBounds.append(bounds);

    QString text;
    int start = std::numeric_limits<int>::max();
    int end = -1;
    qreal glyphX = box.x + line.x;
    for (const auto &glyph : line.glyphs) {
        text += glyph.text;
        if (glyph.textLength > 0) {
            start = qMin(start, glyph.textStart);
            end = qMax(end, glyph.textStart + glyph.textLength);
        }
        const qreal glyphWidth = glyph.width > 0 ? glyph.width : 0;
        if (!glyph.style.linkHref.isEmpty() && glyphWidth > 0) {
            index.linkBounds.append(QRectF(glyphX, box.y + lineY, glyphWidth, qMax(line.height, qreal(1))));
            index.linkHrefs.append(glyph.style.linkHref);
            index.linkTitles.append(glyph.style.linkTitle);
        }
        glyphX += glyphWidth;
    }
    if (start == std::numeric_limits<int>::max())
        start = box.source.startLine;
    if (end < 0)
        end = box.source.endLine;
    index.lineText.append(text);
    index.sourceStart.append(start);
    index.sourceEnd.append(end);
}

void addBlock(HitIndex &index, const Layout::BlockBox &box)
{
    qreal lineY = 0;
    for (const auto &line : box.lines) {
        addLine(index, box, line, lineY);
        lineY += line.height;
    }
}

} // namespace

HitIndex buildHitIndex(const Layout::Page &page)
{
    HitIndex index;
    for (const auto &element : page.elements) {
        if (const auto *block = std::get_if<Layout::BlockBox>(&element))
            addBlock(index, *block);
    }
    return index;
}

int hitLine(const HitIndex &index, QPointF pagePos)
{
    for (int i = 0; i < index.lineBounds.size(); ++i) {
        if (index.lineBounds.at(i).contains(pagePos))
            return i;
    }
    return -1;
}
